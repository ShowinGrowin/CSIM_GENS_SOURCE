#include "ArchiveTreePatcher.h"
#include "Configuration.h"

vector<ArchiveDependency> ArchiveTreePatcher::m_archiveDependencies = 
{
        //{ "SonicClassic_MARZA", { "cmn100", "pam_cmn"} }
        // Must use bb3.ini and add it as #SonicClassic_MARZA for it to load this way.
        // Issue is its so janky it might not really be worth the trouble and I might as well use my old method.
        // Could still be good for adding the dropdash sound on its own along with classic jump.
       // { "SonicClassic_MARZA", { "SonicClassic", "cmn100", "pam_cmn", "evSonicClassic" }}
};

HOOK(bool, __stdcall, ParseArchiveTree, 0xD4C8E0, void* A1, char* pData, const size_t size, void* pDatabase)
{
    std::string str;
    {
        std::stringstream stream;

        for (ArchiveDependency const& node : ArchiveTreePatcher::m_archiveDependencies)
        {
            stream << "  <Node>\n";
            stream << "    <Name>" << node.m_archive << "</Name>\n";
            stream << "    <Archive>" << node.m_archive << "</Archive>\n";
            stream << "    <Order>" << 0 << "</Order>\n";
            stream << "    <DefAppend>" << node.m_archive << "</DefAppend>\n";

            for (string const& dependency : node.m_dependencies)
            {
                stream << "    <Node>\n";
                stream << "      <Name>" << dependency << "</Name>\n";
                stream << "      <Archive>" << dependency << "</Archive>\n";
                stream << "      <Order>" << 0 << "</Order>\n";
                stream << "    </Node>\n";
            }

            stream << "  </Node>\n";
        }

        str = stream.str();
    }

    const size_t newSize = size + str.size();
    const std::unique_ptr<char[]> pBuffer = std::make_unique<char[]>(newSize);
    memcpy(pBuffer.get(), pData, size);

    char* pInsertionPos = strstr(pBuffer.get(), "<Include>");

    memmove(pInsertionPos + str.size(), pInsertionPos, size - (size_t)(pInsertionPos - pBuffer.get()));
    memcpy(pInsertionPos, str.c_str(), str.size());

    bool result;
    {
        result = originalParseArchiveTree(A1, pBuffer.get(), newSize, pDatabase);
    }

    return result;
}

void ArchiveTreePatcher::applyPatches()
{
    // If we be using Marza Classic then load the assets!
   // if (Configuration::cs_model == "graphics_marza")
    //{
    //    m_archiveDependencies.push_back(ArchiveDependency("SonicClassic_MARZA", { "cmn100", "pam_cmn" }));
   // }

    // If we be using the dropdash then load the dropdash sounds!
    //if (Configuration::dropdash_enabled)
    //{
    //    m_archiveDependencies.push_back(ArchiveDependency("DropDashSounds", { "cmn100", "cmn200" }));
   // }
    
    INSTALL_HOOK(ParseArchiveTree);
}