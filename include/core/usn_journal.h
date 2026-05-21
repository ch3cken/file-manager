#pragma once

#include "core/database.h"
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

class UsnJournal {
private:
    DatabaseManager& db;
    bool isInitialized;
    std::string driveLetter;

#ifdef _WIN32
    HANDLE hVol;
    // Issue #18: use USN (LONGLONG / signed) to match the Windows API type,
    // not DWORDLONG (unsigned), avoiding signed/unsigned mismatch.
    USN currentUsn;
    // Issue #11: store journal ID so pollChanges passes it correctly.
    DWORDLONG journalId;

    bool queryUsnJournal();
    std::string getFilePathFromFileId(DWORDLONG fileId);
    void processUsnRecord(void* recordBuffer);
#endif

public:
    UsnJournal(DatabaseManager& database, const std::string& drive = "C:");
    ~UsnJournal();

    // Attempts to initialize the USN Journal listener.
    // Returns false if not on Windows, or if missing Administrator privileges.
    bool init();

    // Polls the journal for new changes since the last update and syncs the database.
    // Returns true if polling was successful.
    bool pollChanges();

    bool isWorking() const { return isInitialized; }
};
