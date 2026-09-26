#pragma once

#include <string>

// librarian-test --check-vocab. Loads the rule files under rulesPath and fails
// when any of them names a tag outside TagVocabulary.h; when scriptPath names
// the JavaScript mirror (modules/tagVocabulary.js), also fails when its lists
// have drifted from the C++ ones. Returns a process exit code.
int CheckVocabulary(const std::string& rulesPath, const std::string& scriptPath);
