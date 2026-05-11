# File Manager
(20230866) Antonio Recuero Buleje
(20220825) Enrique Jose Delgado Garcia
(20230951) Elmoursi Ahmad
(20230959) Murad Ibrahimov

This repo is the implementation of Team 12's File Manager, by Team 7.

## Repository Structure
```
file-manager/
├── src/               # All .cpp and .h files here
│   ├── core/          # NTFS/USN Journal + Database Logic
|   ├── categorization # Categorization logic (Write to DB)
│   ├── search/        # Quick + Smart Search logic (Read from DB)
│   └── ui/            # WebUI interface
├── include/           # Public headers
├── assets/            # Assets for the Overlay UI
├── docs/              # Relevant documents (SRS)
├── tests/             # Test cases (?)
├── vcpkg.json         # Dependency manager
└── CMakeLists.txt     # Build script
```
