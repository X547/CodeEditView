#include <CodeEditView/TextLoader.h>

#include <algorithm>
#include <stdexcept>


namespace CodeEdit {


static constexpr std::string_view kUtf8Bom = "\xef\xbb\xbf";


void TextLoader::Append(std::string_view data)
{
	if (fFinished) {
		throw std::logic_error("TextLoader::Append: already finished");
	}
	if (!fHeadDone) {
		const size_t take = std::min(data.size(), kUtf8Bom.size() - fHead.size());
		fHead.append(data.substr(0, take));
		data.remove_prefix(take);
		if (fHead.size() < kUtf8Bom.size()) {
			return;
		}
		fHeadDone = true;
		fHasBom = fHead == kUtf8Bom;
		if (!fHasBom) {
			AppendText(fHead);
		}
		fHead.clear();
	}
	AppendText(data);
}


// A CR at the end of one piece may start a CRLF ending in the next.
void TextLoader::AppendText(std::string_view data)
{
	size_t pos = 0;
	if (fPendingCr && !data.empty()) {
		fPendingCr = false;
		if (data[0] == '\n') {
			fCrlfCount++;
			pos = 1;
		} else {
			fCrCount++;
		}
		fBuilder.Append("\n");
	}
	while (pos < data.size()) {
		const size_t cr = data.find('\r', pos);
		const std::string_view run = data.substr(pos, (cr == std::string_view::npos ? data.size() : cr) - pos);
		fLfCount += std::count(run.begin(), run.end(), '\n');
		fBuilder.Append(run);
		if (cr == std::string_view::npos) {
			break;
		}
		if (cr + 1 == data.size()) {
			fPendingCr = true;
			break;
		}
		if (data[cr + 1] == '\n') {
			fCrlfCount++;
			pos = cr + 2;
		} else {
			fCrCount++;
			pos = cr + 1;
		}
		fBuilder.Append("\n");
	}
}


void TextLoader::Finish()
{
	if (fFinished) {
		return;
	}
	if (!fHeadDone) {
		fHeadDone = true;
		AppendText(fHead);
		fHead.clear();
	}
	if (fPendingCr) {
		fPendingCr = false;
		fCrCount++;
		fBuilder.Append("\n");
	}
	fFinished = true;
}


Rope TextLoader::TakeText()
{
	if (!fFinished) {
		throw std::logic_error("TextLoader::TakeText: not finished");
	}
	return fBuilder.Build();
}


LineEnding TextLoader::DominantLineEnding() const
{
	if (fCrlfCount > fLfCount && fCrlfCount >= fCrCount) {
		return LineEnding::CRLF;
	}
	if (fCrCount > fLfCount && fCrCount > fCrlfCount) {
		return LineEnding::CR;
	}
	return LineEnding::LF;
}


bool TextLoader::HasMixedLineEndings() const
{
	return (fLfCount > 0) + (fCrlfCount > 0) + (fCrCount > 0) > 1;
}


} // CodeEdit
