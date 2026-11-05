#include "FileDocument.h"

#include <DataIO.h>

#include <CodeEditView/Language.h>

#include "Chunks.h"
#include "Repository.h"


namespace GitDiff {


using CodeEdit::ModelLock;
using CodeEdit::TextModel;


static std::vector<size_t> AllIndexes(const std::vector<Chunk> &chunks)
{
	std::vector<size_t> indexes;
	for (size_t i = 0; i < chunks.size(); i++) {
		indexes.push_back(i);
	}
	return indexes;
}


static std::string ModelText(TextModel &model)
{
	ModelLock lock(model);
	return model.Text().ToString();
}


FileDocument::FileDocument(const std::string &path):
	fPath(path)
{
	for (SideState &state : fSides) {
		state.model.SetTo(new TextModel(), true);
		ModelLock lock(*state.model);
		state.model->SetLanguage(CodeEdit::LanguageForPath(path));
	}
}


bool FileDocument::IsModified(Side side) const
{
	TextModel &model = *State(side).model;
	ModelLock lock(model);
	return model.IsModified();
}


std::string FileDocument::SavedData(Side side) const
{
	TextModel &model = *State(side).model;
	BMallocIO stream;
	{
		ModelLock lock(model);
		model.Save(stream);
	}
	return std::string(static_cast<const char*>(stream.Buffer()), stream.BufferLength());
}


void FileDocument::LoadSide(Side side, const std::optional<std::string> &data, bool force)
{
	SideState &state = State(side);
	const std::string bytes = data.value_or(std::string());
	if (state.loaded == bytes) {
		state.exists = data.has_value();
		return;
	}
	if (!force && IsModified(side)) {
		state.changedOnDisk = true;
		return;
	}
	state.exists = data.has_value();
	state.loaded = bytes;
	state.changedOnDisk = false;
	{
		TextModel &model = *state.model;
		ModelLock lock(model);
		BMemoryIO stream(bytes.data(), bytes.size());
		model.Load(stream);
		model.SetSavePoint();
	}

	// Saving must give back the same bytes, or editing would change lines
	// the user did not touch.
	state.problem.clear();
	if (Repository::IsBinary(bytes)) {
		state.problem = "is a binary file";
	} else if (SavedData(side) != bytes) {
		state.problem = "has mixed line endings";
	}
}


void FileDocument::Load(Repository &repository, bool force)
{
	const std::optional<FileContent> head = repository.ReadHead(fPath);
	const std::optional<FileContent> index = repository.ReadIndex(fPath);
	const std::optional<FileContent> worktree = repository.ReadWorktree(fPath);
	const bool conflicted = repository.IsConflicted(fPath);

	const std::optional<FileContent> contents[] = {head, index, worktree};
	for (int i = 0; i < 3; i++) {
		const Side side = static_cast<Side>(i);
		std::optional<std::string> data;
		if (contents[i].has_value()) {
			data = contents[i]->data;
			State(side).mode = contents[i]->mode;
		}
		// The HEAD version is never edited.
		LoadSide(side, data, force || side == Side::Head);
	}

	// A new index entry gets the line endings that `git add` would store.
	if (!index.has_value() && worktree.has_value() && !IsModified(Side::Index)) {
		TextModel clean;
		const std::string data = repository.CleanWorktreeData(fPath, worktree->data);
		BMemoryIO stream(data.data(), data.size());
		CodeEdit::LineEnding lineEnding;
		bool hasBom;
		{
			ModelLock lock(clean);
			clean.Load(stream);
			lineEnding = clean.GetLineEnding();
			hasBom = clean.HasBom();
		}
		TextModel &model = *State(Side::Index).model;
		ModelLock lock(model);
		model.SetLineEnding(lineEnding);
		model.SetHasBom(hasBom);
	}

	fReadOnlyReason.clear();
	if (conflicted) {
		fReadOnlyReason = "has merge conflicts";
		return;
	}
	for (const std::optional<FileContent> &content : contents) {
		if (!content.has_value()) {
			continue;
		}
		if (content->mode == GIT_FILEMODE_LINK) {
			fReadOnlyReason = "is a symbolic link";
			return;
		}
		if (content->mode == GIT_FILEMODE_COMMIT || content->mode == GIT_FILEMODE_TREE) {
			fReadOnlyReason = "is a submodule";
			return;
		}
	}
	fReadOnlyReason = repository.FilterProblem(fPath);
	for (const SideState &state : fSides) {
		if (fReadOnlyReason.empty() && !state.problem.empty()) {
			fReadOnlyReason = state.problem;
		}
	}
}


void FileDocument::WriteIndex(Repository &repository)
{
	SideState &index = State(Side::Index);
	const SideState &worktree = State(Side::Worktree);
	uint32_t mode = GIT_FILEMODE_BLOB;
	if (index.exists) {
		mode = index.mode;
	} else if (worktree.exists) {
		mode = worktree.mode;
	}
	const std::string data = SavedData(Side::Index);
	repository.WriteIndex(fPath, data, mode);
	index.exists = true;
	index.mode = mode;
	index.loaded = data;
	index.changedOnDisk = false;
	ModelLock lock(*index.model);
	index.model->SetSavePoint();
}


void FileDocument::SaveWorktree(Repository &repository)
{
	SideState &worktree = State(Side::Worktree);
	const std::string data = SavedData(Side::Worktree);
	repository.WriteWorktree(fPath, data);
	worktree.exists = true;
	worktree.loaded = data;
	worktree.changedOnDisk = false;
	ModelLock lock(*worktree.model);
	worktree.model->SetSavePoint();
}


void FileDocument::StageFile(Repository &repository)
{
	if (!Exists(Side::Worktree)) {
		repository.RemoveFromIndex(fPath);
		LoadSide(Side::Index, std::nullopt, true);
		return;
	}
	TextModel &index = *Model(Side::Index);
	TextModel &worktree = *Model(Side::Worktree);
	const std::vector<Chunk> chunks = ComputeChunks(ModelText(index), ModelText(worktree));
	ApplyToA(index, worktree, chunks, AllIndexes(chunks));
	if (Exists(Side::Index)) {
		State(Side::Index).mode = State(Side::Worktree).mode;
	}
	WriteIndex(repository);
}


void FileDocument::UnstageFile(Repository &repository)
{
	if (!Exists(Side::Head)) {
		repository.RemoveFromIndex(fPath);
		LoadSide(Side::Index, std::nullopt, true);
		return;
	}
	TextModel &head = *Model(Side::Head);
	TextModel &index = *Model(Side::Index);
	const std::vector<Chunk> chunks = ComputeChunks(ModelText(head), ModelText(index));
	RevertB(head, index, chunks, AllIndexes(chunks));
	State(Side::Index).exists = true;
	State(Side::Index).mode = State(Side::Head).mode;
	WriteIndex(repository);
}


} // GitDiff
