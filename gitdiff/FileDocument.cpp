#include "FileDocument.h"

#include <DataIO.h>

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
	fPath(path),
	fSides {SideState(path), SideState(path), SideState(path)}
{
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
		State(side).text.Load(data, force || side == Side::Head);
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
		TextModel &model = *Model(Side::Index);
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
		if (fReadOnlyReason.empty() && !state.text.Problem().empty()) {
			fReadOnlyReason = state.text.Problem();
		}
	}
}


void FileDocument::WriteIndex(Repository &repository)
{
	SideState &index = State(Side::Index);
	const SideState &worktree = State(Side::Worktree);
	uint32_t mode = GIT_FILEMODE_BLOB;
	if (index.text.Exists()) {
		mode = index.mode;
	} else if (worktree.text.Exists()) {
		mode = worktree.mode;
	}
	const std::string data = index.text.SavedData();
	repository.WriteIndex(fPath, data, mode);
	index.mode = mode;
	index.text.MarkSaved(data);
}


void FileDocument::SaveWorktree(Repository &repository)
{
	SideState &worktree = State(Side::Worktree);
	const std::string data = worktree.text.SavedData();
	repository.WriteWorktree(fPath, data);
	worktree.text.MarkSaved(data);
}


void FileDocument::StageFile(Repository &repository)
{
	if (!Exists(Side::Worktree)) {
		repository.RemoveFromIndex(fPath);
		State(Side::Index).text.Load(std::nullopt, true);
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
		State(Side::Index).text.Load(std::nullopt, true);
		return;
	}
	TextModel &head = *Model(Side::Head);
	TextModel &index = *Model(Side::Index);
	const std::vector<Chunk> chunks = ComputeChunks(ModelText(head), ModelText(index));
	RevertB(head, index, chunks, AllIndexes(chunks));
	// The entry takes the HEAD mode, also when it was not in the index.
	SideState &indexState = State(Side::Index);
	const uint32_t mode = State(Side::Head).mode;
	const std::string data = indexState.text.SavedData();
	repository.WriteIndex(fPath, data, mode);
	indexState.mode = mode;
	indexState.text.MarkSaved(data);
}


} // GitDiff
