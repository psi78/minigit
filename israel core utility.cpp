// minigit_part1.cpp - Core Utilities, File System Operations & Data Structures
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <openssl/sha.h> // Required for SHA1 hashing
#include <iomanip>
#include <ctime>
#include <queue>         // Not directly used in this part, but part of original includes
#include <unordered_set> // Not directly used in this part, but part of original includes

namespace fs = std::filesystem;

// --- Core Data Structures ---

/**
 * @brief Represents a commit object in the MiniGit repository.
 */
struct CommitObject {
    std::string hash;              // SHA-1 hash of the commit content
    std::string tree_hash;         // SHA-1 hash of the root tree object for this commit
    std::string message;           // Commit message
    std::string author;            // Author information (name <email>)
    std::string committer;         // Committer information (name <email>)
    std::string timestamp;         // Timestamp of the commit
    std::vector<std::string> parent_hashes; // Hashes of parent commits (1 for normal, >1 for merge)
};

/**
 * @brief Represents an entry in the staging area (index).
 */
struct IndexEntry {
    std::string filePath;          // Path to the file
    std::string blobHash;          // SHA-1 hash of the file's content (blob)
};

// --- Global Variables (for simplicity in this divided structure) ---

// The staging area, mapping file paths to their blob hashes
std::map<fs::path, std::string> staging_area;
// The name of the currently active branch
std::string current_branch = "main";

// --- Hashing Utility ---

/**
 * @brief Computes the SHA-1 hash of a given string content.
 * @param content The string content to hash.
 * @return The SHA-1 hash as a hexadecimal string.
 */
std::string computeHash(const std::string& content) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(content.c_str()),
          content.size(), hash);

    std::stringstream ss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

// --- Object Storage (Blobs & Commits) ---

/**
 * @brief Stores content as an object in the .minigit/objects directory.
 * Objects are stored in a Git-like manner: .minigit/objects/XX/YYYYYY...
 * @param hash The SHA-1 hash of the content.
 * @param content The actual content (file blob, tree content, or commit content).
 */
void storeObject(const std::string& hash, const std::string& content) {
    fs::path objectPath = ".minigit/objects/" + hash.substr(0, 2) + "/" + hash.substr(2);
    fs::create_directories(objectPath.parent_path()); // Ensure parent directories exist
    std::ofstream objectFile(objectPath);
    if (!objectFile.is_open()) {
        throw std::runtime_error("Could not open object file for writing: " + objectPath.string());
    }
    objectFile << content;
}

/**
 * @brief Reads content from an object file in the .minigit/objects directory.
 * @param hash The SHA-1 hash of the object to read.
 * @return The content of the object.
 * @throws std::runtime_error if the object file cannot be opened.
 */
std::string readObject(const std::string& hash) {
    fs::path objectPath = ".minigit/objects/" + hash.substr(0, 2) + "/" + hash.substr(2);
    std::ifstream objectFile(objectPath);
    if (!objectFile.is_open()) {
        throw std::runtime_error("Could not open object file for reading: " + objectPath.string());
    }
    std::stringstream ss;
    ss << objectFile.rdbuf(); // Read entire file content into stringstream
    return ss.str();
}

// --- General File I/O Utilities ---

/**
 * @brief Reads the entire content of a file into a string.
 * @param filePath The path to the file.
 * @return The content of the file.
 * @throws std::runtime_error if the file cannot be opened.
 */
std::string readFile(const fs::path& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file for reading: " + filePath.string());
    }
    std::stringstream buffer;
    buffer << file.rdbuf(); // Read entire file content into stringstream
    return buffer.str();
}

/**
 * @brief Writes content to a file, creating parent directories if necessary.
 * @param filePath The path to the file.
 * @param content The string content to write.
 * @throws std::runtime_error if the file cannot be opened for writing.
 */
void writeFile(const fs::path& filePath, const std::string& content) {
    fs::create_directories(filePath.parent_path()); // Ensure parent directories exist
    std::ofstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file for writing: " + filePath.string());
    }
    file << content;
}

/**
 * @brief Splits a string into a vector of strings based on newline characters.
 * (Note: This function is in the original code but not directly used in handlers,
 * kept here as a general utility).
 * @param content The string to split.
 * @return A vector of strings, each representing a line.
 */
std::vector<std::string> splitLines(const std::string& content) {
    std::vector<std::string> lines;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

// --- Index (Staging Area) Management ---

/**
 * @brief Reads the .minigit/index file and populates the global staging_area map.
 * Clears the current staging_area before reading.
 */
void readIndex() {
    staging_area.clear(); // Clear existing entries
    std::ifstream indexFile(".minigit/index");
    if (!indexFile.is_open()) return; // If index file doesn't exist, assume empty staging area

    std::string line;
    while (std::getline(indexFile, line)) {
        size_t spacePos = line.find(' ');
        if (spacePos != std::string::npos) {
            std::string filePath = line.substr(0, spacePos);
            std::string blobHash = line.substr(spacePos + 1);
            staging_area[filePath] = blobHash; // Add to staging area
        }
    }
}

/**
 * @brief Updates (overwrites) the .minigit/index file based on the current staging_area map.
 * @throws std::runtime_error if the index file cannot be opened for writing.
 */
void updateIndex() {
    std::ofstream indexFile(".minigit/index", std::ios::trunc); // Truncate (clear) file
    if (!indexFile.is_open()) {
        throw std::runtime_error("Could not open .minigit/index for writing.");
    }
    for (const auto& entry : staging_area) {
        indexFile << entry.first.string() << " " << entry.second << "\n";
    }
}

// --- Working Directory Management Helpers ---

/**
 * @brief Extracts all file paths (keys) from a map of file paths to hashes.
 * @param filesMap A map where keys are file paths.
 * @return A set of file paths.
 */
std::set<fs::path> getPaths(const std::map<fs::path, std::string>& filesMap) {
    std::set<fs::path> paths;
    for (const auto& pair : filesMap) {
        paths.insert(pair.first);
    }
    return paths;
}

/**
 * @brief Cleans the current working directory by removing files not specified in keepPaths.
 * It also attempts to remove empty directories.
 * @param keepPaths A set of file paths that should *not* be removed from the working directory.
 */
void cleanWorkingDirectory(const std::set<fs::path>& keepPaths) {
    std::vector<fs::path> filesToDelete;
    for (const auto& entry : fs::recursive_directory_iterator(fs::current_path())) {
        // Skip .minigit directory itself
        if (entry.path().string().find(".minigit") != std::string::npos) {
            continue;
        }
        // If it's a regular file and not in the keepPaths set, mark for deletion
        if (entry.is_regular_file() && keepPaths.find(entry.path()) == keepPaths.end()) {
            filesToDelete.push_back(entry.path());
        }
    }

    // Delete marked files
    for (const auto& path : filesToDelete) {
        try {
            fs::remove(path);
        } catch (const fs::filesystem_error& e) {
            std::cerr << "Warning: Could not remove file " << path.string() << ": " << e.what() << std::endl;
        }
    }

    // Collect and sort directories by length (descending) to delete empty ones safely
    std::vector<fs::path> dirs;
    for (const auto& entry : fs::recursive_directory_iterator(fs::current_path())) {
        if (entry.path().string().find(".minigit") != std::string::npos) {
            continue;
        }
        if (entry.is_directory()) {
            dirs.push_back(entry.path());
        }
    }
    // Sort directories by length descending to ensure inner directories are deleted first
    std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) {
        return a.string().length() > b.string().length();
    });

    // Attempt to remove empty directories
    for (const auto& dir : dirs) {
        try {
            if (fs::is_empty(dir)) {
                fs::remove(dir);
            }
        } catch (const fs::filesystem_error& e) {
            // Ignore errors for non-empty or permission-denied directories
        }
    }
}

/**
 * @brief Restores files to the working directory from the object store.
 * @param filesToRestore A map where keys are target file paths and values are blob hashes.
 */
void restoreFiles(const std::map<fs::path, std::string>& filesToRestore) {
    for (const auto& [path, hash] : filesToRestore) {
        std::string content = readObject(hash); // Read content of the blob
        writeFile(path, content);             // Write content to the specified path
    }
}
