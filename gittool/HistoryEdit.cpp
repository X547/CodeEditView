#include "HistoryEdit.h"

#include <algorithm>


namespace GitTool {


static CommitPtr LookupCommit(git_repository *repository, const git_oid &id)
{
	git_commit *commit = nullptr;
	Check(git_commit_lookup(&commit, repository, &id), "Cannot read the commit");
	return CommitPtr(commit);
}


static TreePtr CommitTree(git_commit *commit)
{
	git_tree *tree = nullptr;
	Check(git_commit_tree(&tree, commit), "Cannot read the commit tree");
	return TreePtr(tree);
}


static std::string TrimEnd(std::string text)
{
	while (!text.empty() && (text.back() == '\n' || text.back() == ' ' || text.back() == '\t')) {
		text.pop_back();
	}
	return text;
}


HistoryEdit::HistoryEdit(git_repository *repository, const std::string &branch, size_t limit):
	fRepository(repository),
	fBranch(branch)
{
	Check(git_reference_name_to_id(&fTip, repository, ("refs/heads/" + branch).c_str()), "Cannot read the branch");
	git_oid id = fTip;
	while (true) {
		if (fOriginal.size() >= limit) {
			fBase = id;
			break;
		}
		CommitPtr commit = LookupCommit(repository, id);
		const unsigned parents = git_commit_parentcount(commit.get());
		if (parents > 1) {
			fBase = id;
			break;
		}
		fOriginal.push_back(id);
		if (parents == 0) {
			break;
		}
		id = *git_commit_parent_id(commit.get(), 0);
	}
	std::reverse(fOriginal.begin(), fOriginal.end());
	Reset();
}


void HistoryEdit::Reset()
{
	fSteps.clear();
	for (const git_oid &id : fOriginal) {
		EditStep step;
		step.commit = id;
		fSteps.push_back(step);
	}
	Replay();
}


void HistoryEdit::Move(const std::vector<size_t> &indexes, size_t position)
{
	std::vector<size_t> sorted = indexes;
	std::sort(sorted.begin(), sorted.end());
	sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
	std::vector<EditStep> moved;
	size_t before = 0;
	for (size_t index : sorted) {
		moved.push_back(fSteps.at(index));
		before += index < position ? 1 : 0;
	}
	for (auto it = sorted.rbegin(); it != sorted.rend(); it++) {
		fSteps.erase(fSteps.begin() + *it);
	}
	position = std::min(position - before, fSteps.size());
	fSteps.insert(fSteps.begin() + position, moved.begin(), moved.end());
	Replay();
}


void HistoryEdit::Insert(const std::vector<git_oid> &commits, size_t position)
{
	std::vector<EditStep> inserted;
	for (const git_oid &id : commits) {
		CommitPtr commit = LookupCommit(fRepository, id);
		if (git_commit_parentcount(commit.get()) > 1) {
			throw GitError("Commit " + ShortId(id) + " is a merge; merges cannot be copied");
		}
		EditStep step;
		step.commit = id;
		step.copied = true;
		inserted.push_back(step);
	}
	position = std::min(position, fSteps.size());
	fSteps.insert(fSteps.begin() + position, inserted.begin(), inserted.end());
	Replay();
}


void HistoryEdit::Remove(const std::vector<size_t> &indexes)
{
	std::vector<size_t> sorted = indexes;
	std::sort(sorted.begin(), sorted.end());
	sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
	for (auto it = sorted.rbegin(); it != sorted.rend(); it++) {
		fSteps.erase(fSteps.begin() + *it);
	}
	Replay();
}


void HistoryEdit::SetSquash(size_t index, bool squash)
{
	fSteps.at(index).squash = squash && index > 0;
	Replay();
}


bool ConflictFile::SameVersions(const ConflictFile &other) const
{
	return path == other.path && git_oid_equal(&ancestor, &other.ancestor) && git_oid_equal(&ours, &other.ours)
		&& git_oid_equal(&theirs, &other.theirs);
}


void HistoryEdit::SetFiles(size_t index, const std::map<std::string, FileOverride> &files)
{
	EditStep &step = fSteps.at(index);
	for (const auto &[path, file] : files) {
		step.files[path] = file;
	}
	Replay();
}


void HistoryEdit::Split(size_t index, const std::map<std::string, FileOverride> &firstPart,
	const std::string &firstMessage)
{
	EditStep first = fSteps.at(index);
	EditStep second = first;
	for (const auto &[path, file] : firstPart) {
		first.files[path] = file;
	}
	first.message = firstMessage;
	second.squash = false;
	second.completesPrevious = true;
	fSteps[index] = first;
	fSteps.insert(fSteps.begin() + index + 1, second);
	Replay();
}


void HistoryEdit::SetMessage(size_t index, const std::string &message)
{
	EditStep &step = fSteps.at(index);
	CommitPtr commit = LookupCommit(fRepository, step.commit);
	const char *original = git_commit_message(commit.get());
	// The original message again leaves the step untouched.
	if (!step.squash && TrimEnd(message) == TrimEnd(original != nullptr ? original : "")) {
		step.message.reset();
	} else {
		step.message = message;
	}
	Replay();
}


std::optional<size_t> HistoryEdit::FirstChanged() const
{
	const size_t count = std::max(fSteps.size(), fOriginal.size());
	for (size_t i = 0; i < count; i++) {
		if (i >= fSteps.size() || i >= fOriginal.size()) {
			return i;
		}
		const EditStep &step = fSteps[i];
		if (step.squash && i > 0) {
			// The commit it is combined with changes too.
			return i - 1;
		}
		if (!git_oid_equal(&step.commit, &fOriginal[i]) || step.squash || step.message.has_value() || step.copied
			|| !step.files.empty() || step.completesPrevious) {
			return i;
		}
	}
	return std::nullopt;
}


std::string HistoryEdit::Message(size_t index) const
{
	const EditStep &step = fSteps.at(index);
	if (step.message.has_value()) {
		return *step.message;
	}
	CommitPtr commit = LookupCommit(fRepository, step.commit);
	const char *message = git_commit_message(commit.get());
	return message != nullptr ? message : "";
}


static std::vector<ConflictFile> Conflicts(git_index *index)
{
	std::vector<ConflictFile> conflicts;
	git_index_conflict_iterator *iterator = nullptr;
	if (git_index_conflict_iterator_new(&iterator, index) < 0) {
		git_error_clear();
		return conflicts;
	}
	const git_index_entry *ancestor = nullptr;
	const git_index_entry *ours = nullptr;
	const git_index_entry *theirs = nullptr;
	while (git_index_conflict_next(&ancestor, &ours, &theirs, iterator) == 0) {
		ConflictFile conflict;
		const git_index_entry *entry = theirs != nullptr ? theirs : ours != nullptr ? ours : ancestor;
		conflict.path = entry->path;
		if (ancestor != nullptr) {
			conflict.ancestor = ancestor->id;
			conflict.ancestorMode = ancestor->mode;
		}
		if (ours != nullptr) {
			conflict.ours = ours->id;
			conflict.oursMode = ours->mode;
		}
		if (theirs != nullptr) {
			conflict.theirs = theirs->id;
			conflict.theirsMode = theirs->mode;
		}
		conflicts.push_back(std::move(conflict));
	}
	git_index_conflict_iterator_free(iterator);
	return conflicts;
}


static void ApplyOverride(git_index *index, const std::string &path, const FileOverride &file)
{
	if (!file.blob.has_value()) {
		if (git_index_remove(index, path.c_str(), 0) < 0) {
			git_error_clear();
		}
		return;
	}
	git_index_entry entry {};
	entry.path = path.c_str();
	entry.mode = file.mode;
	entry.id = *file.blob;
	Check(git_index_add(index, &entry), "Cannot apply the edited file");
}


// Resolves the conflicts the step has overrides for, then applies its other
// overrides; returns the conflicts that remain.
static std::vector<ConflictFile> ApplyOverrides(git_index *index, const EditStep &step)
{
	std::vector<ConflictFile> remaining;
	for (const ConflictFile &conflict : Conflicts(index)) {
		const auto found = step.files.find(conflict.path);
		const bool resolved = found != step.files.end()
			&& (!found->second.resolves.has_value() || found->second.resolves->SameVersions(conflict));
		if (!resolved) {
			remaining.push_back(conflict);
			continue;
		}
		Check(git_index_conflict_remove(index, conflict.path.c_str()), "Cannot resolve the conflict");
		ApplyOverride(index, conflict.path, found->second);
	}
	for (const auto &[path, file] : step.files) {
		if (!file.resolves.has_value()) {
			ApplyOverride(index, path, file);
		}
	}
	return remaining;
}


void HistoryEdit::Replay()
{
	fResults.assign(fSteps.size(), StepResult());
	fNewTip.reset();
	fError.clear();
	const std::optional<size_t> first = FirstChanged();
	const size_t replayStart = first.value_or(fSteps.size());
	for (size_t i = 0; i < replayStart && i < fSteps.size(); i++) {
		fResults[i].commit = fSteps[i].commit;
		fResults[i].parent = i == 0 ? fBase : std::optional<git_oid>(fSteps[i - 1].commit);
	}
	if (!first.has_value()) {
		fNewTip = fTip;
		return;
	}

	std::optional<git_oid> current = replayStart == 0 ? fBase : std::optional<git_oid>(fOriginal[replayStart - 1]);
	git_signature *committerSignature = nullptr;
	if (git_signature_default(&committerSignature, fRepository) < 0) {
		git_error_clear();
		fError = "Set user.name and user.email in the Git configuration to rewrite commits";
		for (size_t i = replayStart; i < fSteps.size(); i++) {
			fResults[i].state = StepState::Pending;
		}
		return;
	}
	SignaturePtr committer(committerSignature);

	// The commit the previous step was replayed onto, for completing a
	// split.
	std::optional<git_oid> previousBase;
	// The commit at current when this replay created it, for squashing.
	std::vector<size_t> group;
	std::optional<git_oid> groupParent;
	std::string groupMessage;
	SignaturePtr groupAuthor;

	for (size_t i = replayStart; i < fSteps.size(); i++) {
		const EditStep &step = fSteps[i];
		StepResult &result = fResults[i];
		try {
			CommitPtr commit = LookupCommit(fRepository, step.commit);
			if (git_commit_parentcount(commit.get()) > 1) {
				throw GitError("merge commits cannot be replayed");
			}
			TreePtr ancestor;
			if (git_commit_parentcount(commit.get()) == 1) {
				CommitPtr parent = LookupCommit(fRepository, *git_commit_parent_id(commit.get(), 0));
				ancestor = CommitTree(parent.get());
			}
			// The second part of a split applies the whole change onto what the
			// first part was applied onto; the first part is already in it.
			const bool completes = step.completesPrevious && i > 0
				&& git_oid_equal(&fSteps[i - 1].commit, &step.commit);
			const std::optional<git_oid> onto = completes ? previousBase : current;
			previousBase = current;
			TreePtr ours;
			if (onto.has_value()) {
				CommitPtr ontoCommit = LookupCommit(fRepository, *onto);
				ours = CommitTree(ontoCommit.get());
			}
			TreePtr currentTree;
			if (current.has_value()) {
				CommitPtr currentCommit = LookupCommit(fRepository, *current);
				currentTree = CommitTree(currentCommit.get());
			}
			TreePtr theirs = CommitTree(commit.get());

			git_merge_options options = GIT_MERGE_OPTIONS_INIT;
			git_index *index = nullptr;
			Check(git_merge_trees(&index, fRepository, ancestor.get(), ours.get(), theirs.get(), &options),
				"Cannot apply the commit");
			IndexPtr indexPtr(index);
			const std::vector<ConflictFile> conflicts = ApplyOverrides(index, step);
			if (!conflicts.empty()) {
				result.state = StepState::Conflict;
				result.parent = current;
				result.conflictFiles = conflicts;
				for (const ConflictFile &conflict : conflicts) {
					result.conflicts.push_back(conflict.path);
				}
				for (size_t j = i + 1; j < fSteps.size(); j++) {
					fResults[j].state = StepState::Pending;
				}
				return;
			}
			git_oid treeId;
			Check(git_index_write_tree_to(&treeId, index, fRepository), "Cannot write the tree");

			const bool squash = step.squash && !group.empty();
			if (currentTree != nullptr && git_oid_equal(&treeId, git_tree_id(currentTree.get()))
				&& !step.message.has_value()) {
				result.state = StepState::Empty;
				result.commit = current.value_or(git_oid {});
				result.parent = current;
				continue;
			}

			const char *originalMessage = git_commit_message(commit.get());
			std::string message;
			if (step.message.has_value()) {
				message = *step.message;
			} else if (squash) {
				message = TrimEnd(groupMessage) + "\n\n" + (originalMessage != nullptr ? originalMessage : "");
			} else {
				message = originalMessage != nullptr ? originalMessage : "";
			}
			const std::optional<git_oid> parentId = squash ? groupParent : current;
			CommitPtr parentCommit;
			if (parentId.has_value()) {
				parentCommit = LookupCommit(fRepository, *parentId);
			}
			const git_commit *parents[] = {parentCommit.get()};
			git_tree *tree = nullptr;
			Check(git_tree_lookup(&tree, fRepository, &treeId), "Cannot read the tree");
			TreePtr treePtr(tree);
			const git_signature *author = squash ? groupAuthor.get() : git_commit_author(commit.get());
			git_oid newId;
			Check(git_commit_create(&newId, fRepository, nullptr, author, committer.get(), nullptr, message.c_str(),
				tree, parentId.has_value() ? 1 : 0, parents), "Cannot create the commit");

			if (squash) {
				for (size_t member : group) {
					fResults[member].commit = newId;
					fResults[member].parent = parentId;
				}
				group.push_back(i);
			} else {
				groupParent = current;
				group = {i};
				git_signature *copy = nullptr;
				Check(git_signature_dup(&copy, git_commit_author(commit.get())), "Cannot copy the author");
				groupAuthor.reset(copy);
			}
			groupMessage = message;
			current = newId;
			result.state = StepState::Rewritten;
			result.commit = newId;
			result.parent = parentId;
		} catch (const GitError &e) {
			result.state = StepState::Conflict;
			result.parent = current;
			result.conflicts = {e.what()};
			for (size_t j = i + 1; j < fSteps.size(); j++) {
				fResults[j].state = StepState::Pending;
			}
			return;
		}
	}
	fNewTip = current;
}


std::string HistoryEdit::Problem() const
{
	if (!fError.empty()) {
		return fError;
	}
	if (!HasChanges()) {
		return "There are no changes";
	}
	for (size_t i = 0; i < fResults.size(); i++) {
		if (fResults[i].state == StepState::Conflict) {
			return "Commit " + ShortId(fSteps[i].commit) + " conflicts; resolve it, remove it or move it";
		}
	}
	if (!fNewTip.has_value()) {
		return "The branch would have no commits";
	}
	return std::string();
}


Plan HistoryEdit::PlanApply(const Snapshot &snapshot) const
{
	Plan plan;
	plan.title = "Apply history edits to branch " + fBranch;
	const BranchInfo *branch = snapshot.FindBranch(fBranch);
	if (branch == nullptr || !git_oid_equal(&branch->commit, &fTip)) {
		plan.blocked = "Branch " + fBranch + " changed since editing started; reset the edits";
		return plan;
	}
	plan.blocked = Problem();
	if (!plan.blocked.empty()) {
		return plan;
	}
	size_t rewritten = 0;
	size_t empty = 0;
	for (const StepResult &result : fResults) {
		rewritten += result.state == StepState::Rewritten ? 1 : 0;
		empty += result.state == StepState::Empty ? 1 : 0;
	}
	const size_t first = *FirstChanged();
	const std::optional<git_oid> start = first == 0 ? fBase : std::optional<git_oid>(fOriginal[first - 1]);
	std::string step = "Replay " + std::to_string(fSteps.size() - first) + " commits on "
		+ (start.has_value() ? Describe(fRepository, *start) : std::string("an empty history"))
		+ ": " + std::to_string(rewritten) + " written";
	if (empty > 0) {
		step += ", " + std::to_string(empty) + " left out as empty";
	}
	plan.steps.push_back(step);
	PlanBranchMove(fRepository, snapshot, *branch, *fNewTip, plan);
	const std::string name = fBranch;
	const git_oid oldId = fTip;
	const git_oid newId = *fNewTip;
	plan.run = [name, oldId, newId](RunContext &context) {
		RunBranchMove(context, name, oldId, newId, "gittool: edit history");
	};
	return plan;
}


} // GitTool
