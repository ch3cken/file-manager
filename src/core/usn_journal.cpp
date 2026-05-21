#include "core/usn_journal.h"
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#include <filesystem>
#include <chrono>
#include <sstream>

namespace fs = std::filesystem;

// Helper from indexer.cpp
extern std::string formatFileTime(const fs::file_time_type& ftime);

#endif

UsnJournal::UsnJournal(DatabaseManager& database, const std::string& drive)
    : db(database), isInitialized(false), driveLetter(drive) {
#ifdef _WIN32
    hVol = INVALID_HANDLE_VALUE;
    currentUsn = 0;
    journalId  = 0;
#endif
}

UsnJournal::~UsnJournal() {
#ifdef _WIN32
    if (hVol != INVALID_HANDLE_VALUE) {
        CloseHandle(hVol);
    }
#endif
}

bool UsnJournal::init() {
#ifdef _WIN32
    // Issue #10: build "\\.\X:" robustly regardless of whether driveLetter
    // was given as "C", "C:", or "C:\".
    std::string base = driveLetter;
    // Strip any trailing backslash
    while (!base.empty() && (base.back() == '\\' || base.back() == '/')) {
        base.pop_back();
    }
    // Ensure trailing colon
    if (base.empty() || base.back() != ':') {
        base += ':';
    }
    std::string volPath = "\\\\.\\" + base;

    hVol = CreateFileA(
        volPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hVol == INVALID_HANDLE_VALUE) {
        // Fallback to read-only if GENERIC_WRITE is denied (restricted admin)
        hVol = CreateFileA(
            volPath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
    }

    if (hVol == INVALID_HANDLE_VALUE) {
        std::cerr << "[USN Journal] Failed to open volume " << volPath
                  << ". Error: " << GetLastError() << std::endl;
        std::cerr << "[USN Journal] Real-time sync disabled. Falling back to static scanner." << std::endl;
        return false;
    }

    if (!queryUsnJournal()) {
        CloseHandle(hVol);
        hVol = INVALID_HANDLE_VALUE;
        return false;
    }

    isInitialized = true;
    std::cout << "[USN Journal] Successfully initialized real-time listener on " << driveLetter << std::endl;
    return true;
#else
    std::cerr << "[USN Journal] Not running on Windows. Real-time sync disabled." << std::endl;
    return false;
#endif
}

#ifdef _WIN32
bool UsnJournal::queryUsnJournal() {
    USN_JOURNAL_DATA_V0 usnData;
    DWORD bytesReturned;

    if (!DeviceIoControl(hVol, FSCTL_QUERY_USN_JOURNAL, NULL, 0,
                         &usnData, sizeof(usnData), &bytesReturned, NULL)) {
        std::cerr << "[USN Journal] FSCTL_QUERY_USN_JOURNAL failed. Error: "
                  << GetLastError() << std::endl;
        return false;
    }

    currentUsn = usnData.NextUsn;
    // Issue #11: store the journal ID so pollChanges can pass it correctly.
    journalId  = usnData.UsnJournalID;
    return true;
}

std::string UsnJournal::getFilePathFromFileId(DWORDLONG fileId) {
    FILE_ID_DESCRIPTOR fileIdDesc;
    fileIdDesc.Type = FileIdType;
    fileIdDesc.FileId.QuadPart = fileId;
    fileIdDesc.dwSize = sizeof(fileIdDesc);

    HANDLE hFile = OpenFileById(hVol, &fileIdDesc,
                                GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, 0);
    if (hFile == INVALID_HANDLE_VALUE) {
        return "";
    }

    // Issue #17: GetFinalPathNameByHandleW may return paths longer than MAX_PATH.
    // First call with a small buffer to get the required length, then allocate.
    DWORD needed = GetFinalPathNameByHandleW(hFile, nullptr, 0, VOLUME_NAME_DOS);
    if (needed == 0) {
        CloseHandle(hFile);
        return "";
    }

    std::wstring wPath(needed, L'\0');
    DWORD pathLen = GetFinalPathNameByHandleW(hFile, &wPath[0],
                                              static_cast<DWORD>(wPath.size()),
                                              VOLUME_NAME_DOS);
    CloseHandle(hFile);

    if (pathLen == 0 || pathLen >= wPath.size()) {
        return "";
    }
    wPath.resize(pathLen);

    // Convert wide string to UTF-8
    int utf8Size = WideCharToMultiByte(CP_UTF8, 0, wPath.c_str(), -1,
                                       nullptr, 0, NULL, NULL);
    if (utf8Size <= 0) return "";
    std::string fullPath(utf8Size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wPath.c_str(), -1,
                        &fullPath[0], utf8Size, NULL, NULL);

    // Remove the "\\?\" prefix added by GetFinalPathNameByHandleW
    if (fullPath.rfind("\\\\?\\", 0) == 0) {
        fullPath = fullPath.substr(4);
    }
    return fullPath;
}

void UsnJournal::processUsnRecord(void* recordBuffer) {
    PUSN_RECORD_V2 pRecord = (PUSN_RECORD_V2)recordBuffer;

    // Issue #12: fix operator precedence — each flag must be individually
    // AND-ed with Reason before OR-ing the results together.
    // Issue #14: also handle DATA_OVERWRITE / DATA_EXTEND (file modifications).
    bool isCreate  = (pRecord->Reason & USN_REASON_FILE_CREATE)     != 0;
    bool isDelete  = (pRecord->Reason & USN_REASON_FILE_DELETE)      != 0;
    bool isRename  = (pRecord->Reason & USN_REASON_RENAME_NEW_NAME)  != 0;
    bool isModify  = (pRecord->Reason & USN_REASON_DATA_OVERWRITE)   != 0
                  || (pRecord->Reason & USN_REASON_DATA_EXTEND)      != 0;

    if (!isCreate && !isDelete && !isRename && !isModify) {
        return;
    }

    // Issue #16: guard against overflowing the charName buffer.
    int nameLen = pRecord->FileNameLength / sizeof(WCHAR);
    if (nameLen <= 0 || nameLen >= MAX_PATH) {
        return;
    }

    char charName[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, pRecord->FileName, nameLen,
                        charName, MAX_PATH - 1, NULL, NULL);
    charName[nameLen] = '\0';
    std::string fileName(charName);

    if (isDelete) {
        // Issue #13: deleting by file_name alone can match unrelated files with the
        // same name in different directories. Ideally we would store and match by
        // FileReferenceNumber. As a safer interim, also log the ambiguity.
        // A full fix requires adding a file_ref_number column to the DB.
        std::cerr << "[USN Journal] Warning: deleting by name '" << fileName
                  << "' — may affect multiple entries if names collide." << std::endl;
        const char* sql = "DELETE FROM files WHERE file_name = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db.getDb(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, fileName.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            std::cout << "[USN Journal] Deleted file record: " << fileName << std::endl;
        }
    } else {
        // Creation, rename, or modification — resolve full path and upsert.
        std::string fullPath = getFilePathFromFileId(pRecord->FileReferenceNumber);
        if (!fullPath.empty()) {
            FileRecord record;
            record.file_path = fullPath;
            record.file_name = fileName;

            try {
                fs::path p(fullPath);
                if (p.has_extension()) record.extension = p.extension().string();
                record.last_modified = formatFileTime(fs::last_write_time(p));
            } catch (...) {
                record.last_modified = "1970-01-01 00:00:00";
            }

            if (db.insertFile(record)) {
                std::cout << "[USN Journal] Synchronized file: " << fullPath << std::endl;
            }
        }
    }
}
#endif

bool UsnJournal::pollChanges() {
    if (!isInitialized) return false;

#ifdef _WIN32
    READ_USN_JOURNAL_DATA_V0 readData = {0};
    readData.StartUsn       = currentUsn;
    readData.ReasonMask     = 0xFFFFFFFF; // Monitor all reasons
    readData.ReturnOnlyOnClose = FALSE;
    readData.Timeout        = 0;
    readData.BytesToWaitFor = 0;
    // Issue #11: use the real journal ID retrieved during init, not 0.
    readData.UsnJournalID   = journalId;

    BYTE  buffer[4096];
    DWORD bytesReturned;

    if (DeviceIoControl(hVol, FSCTL_READ_USN_JOURNAL,
                        &readData, sizeof(readData),
                        buffer, sizeof(buffer), &bytesReturned, NULL)) {
        if (bytesReturned < sizeof(USN)) {
            return true; // No new records
        }

        // The first 8 bytes are the next USN
        USN nextUsn = *(USN*)buffer;

        DWORD recordOffset = sizeof(USN);
        while (recordOffset < bytesReturned) {
            PUSN_RECORD_V2 pRecord = (PUSN_RECORD_V2)(buffer + recordOffset);
            if (pRecord->RecordLength == 0) break; // safety guard against infinite loop
            processUsnRecord(pRecord);
            recordOffset += pRecord->RecordLength;
        }

        currentUsn = nextUsn; // Checkpoint
        return true;
    } else {
        DWORD err = GetLastError();
        if (err != ERROR_HANDLE_EOF) { // EOF means no more data, which is normal
            std::cerr << "[USN Journal] Failed to read journal. Error: " << err << std::endl;
            return false;
        }
        return true;
    }
#else
    return false;
#endif
}
