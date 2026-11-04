#pragma once

#include <stdint.h>

#include <map>
#include <set>
#include <string>

#include "Common.h"


class BRow;


namespace Debugger {


class VariablesList;


// Scopes and variables of the selected frame as a tree; children are
// loaded when a row is expanded. Expanded rows stay expanded across stops,
// changed values are highlighted.
class VariablesWindow: public ToolWindow {
private:
	VariablesList *fList;
	// Rows waiting for their children, by variables reference.
	std::map<int64_t, BRow*> fLoading;
	std::set<std::string> fExpanded;
	std::map<std::string, std::string> fValues;
	std::map<std::string, std::string> fPreviousValues;

	void SetScopes(const BMessage &notice);
	void AddVariables(const BMessage &notice);
	BRow *AddRow(BRow *parent, const std::string &path, const char *name, const char *value, const char *type, int64_t reference);

public:
	VariablesWindow();

	// Called by the list when a row is expanded or collapsed.
	void RowExpanded(BRow *row, bool expanded);

	void MessageReceived(BMessage *message) override;
};


} // Debugger
