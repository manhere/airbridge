#pragma once

// Variable catalogue. VarCatalog.cpp is GENERATED from ../../src/settings_defs.h
// by gen_catalog.py - do not hand-edit the table.

#include <map>
#include <string>
#include <vector>

struct VarDef {
    std::string name;
    std::string label;                  // stock English label
    std::string cn;                     // Chinese label (unchanged when no accepted translation)
    bool        isEnum = false;
    std::vector<std::string> options;
};

class CVarCatalog {
public:
    static const CVarCatalog& Instance();
    const VarDef* Find(const std::string& name) const;
    const std::vector<VarDef>& All() const { return m_vars; }

private:
    CVarCatalog();
    std::vector<VarDef> m_vars;
    std::map<std::string, size_t> m_index;
};

inline std::string VarDisplayName(const std::string& name)
{
    const VarDef* def = CVarCatalog::Instance().Find(name);
    if (!def) return name;
    return def->cn.empty() ? def->label : def->cn;
}
