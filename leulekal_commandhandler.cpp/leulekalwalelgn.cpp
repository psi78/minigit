// minigit_part2.cpp - Tree and Commit Object Logic
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
#include <queue>
#include <unordered_set>

namespace fs = std::filesystem;

// --- DUPICATED FROM PART 1 FOR SELF-CONTAINMENT (Ideally would be in a header) ---
struct CommitObject {
    std::string hash;
    std::string tree_hash;
    std::string message;
    std::string author;
    std::string committer;
    std::string timestamp;
    std::vector<std::string> parent_hashes;
};

std::string computeHash(const std::string& content) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(content.c_str()), content.size(), hash);
    std::stringstream ss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

void storeObject(const std::string& hash, const std::string& content) {
    fs::path objectPath = ".minigit/objects/" + hash.substr(0, 2) + "/" + hash.substr(2);
    fs::create_directories(objectPath.parent_path());
    std::ofstream objectFile(objectPath);
    if (!objectFile.is_open()) {
        throw std::runtime_error("Could not open object file for writing: " + objectPath.string());
    }
    objectFile << content;
}

std::string readObject(const std::string& hash) {
    fs::path objectPath = ".minigit/objects/" + hash.substr(0, 2) + "/" + hash.substr(2);
    std::ifstream objectFile(objectPath);
    if (!objectFile.is_open()) {
        throw std::runtime_error("Could not open object file for reading: " + objectPath.string());
    }
    std::stringstream ss;
    ss << objectFile.rdbuf();
    return ss.str();
}
// --- END DUPLICATION ---

// --- Tree Object Management ---

/**
 * @brief Recursively retrieves all files (blobs) contained within a given tree object.
 * This function reconstructs the file paths relative to the basePath.
 * @param treeHash The SHA-1 hash of the tree object.
 * @param basePath The base path to prepend to file/subdirectory names within this tree.
 * @return A map where keys are full file paths and values are their blob hashes.
 * @throws std::runtime_error if the tree object cannot be read.
 */
std::map<fs::path, std::string> getTreeFiles(const std::string& treeHash, const fs::path& basePath = "") {
    std::map<fs::path, std::string> files;
    std::string treeContent = readObject(treeHash); // Read the tree object content
    std::istringstream contentStream(treeContent);
    std::string line;

    while (std::getline(contentStream, line)) {
        // Parse format: <mode> <type> <hash> <name>
        size_t firstSpace = line.find(' ');
        size_t secondSpace = line.find(' ', firstSpace + 1);
        if (firstSpace == std::string::npos || secondSpace == std::string::npos) continue; // Malformed line

        std::string type = line.substr(firstSpace + 1, secondSpace - firstSpace - 1); // "blob" or "tree"
        std::string hash = line.substr(secondSpace + 1, 40);                         // SHA-1 hash (40 chars)
        std::string name = line.substr(secondSpace + 42);                           // File/directory name

        fs::path fullPath = basePath / name; // Construct the full path

        if (type == "blob") {
            files[fullPath] = hash; // If it's a blob, add to the map
        } else if (type == "tree") {
            // If it's a tree, recursively call getTreeFiles for the subtree
            auto subtreeFiles = getTreeFiles(hash, fullPath);
            files.insert(subtreeFiles.begin(), subtreeFiles.end()); // Add all files from subtree
        }
    }
    return files;
}

/**
 * @brief Creates a hierarchy of tree objects from a flat map of files and their hashes.
 * This function simulates how Git builds its tree objects for directories.
 * @param files A map where keys are file paths and values are their blob hashes (like the staging area).
 * @return The SHA-1 hash of the root tree object created. Returns empty string if no files.
 */
std::string createTreeFromFiles(const std::map<fs::path, std::string>& files) {
    // Map to hold entries for each directory, with file/sub-tree names mapped to their hashes
    std::map<std::string, std::map<std::string, std::string>> dirEntries;

    // Populate dirEntries with blobs
    for (const auto& [path, hash] : files) {
        fs::path dir = path.parent_path();
        if (dir.empty()) dir = "."; // Treat root level files as being in "." directory
        dirEntries[dir.string()][path.filename().string()] = hash; // Add file (blob) to its parent directory's entries
    }

    // Collect all unique directory paths and sort them by length descending
    // This ensures child directories are processed before their parents
    std::vector<std::string> dirs;
    for (const auto& [dir, _] : dirEntries) {
        dirs.push_back(dir);
    }
    std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) {
        return a.length() > b.length();
    });

    // Map to store hashes of created tree objects (directory paths to tree hashes)
    std::map<std::string, std::string> treeHashes;

    // Iterate through directories (from deepest to root) to create tree objects
    for (const auto& dir : dirs) {
        std::stringstream treeContent;
        
        // Add blob entries for files directly in this directory
        for (const auto& [name, hash] : dirEntries[dir]) {
            treeContent << "100644 blob " << hash << " " << name << "\n"; // Standard mode for files
        }
        
        // Add tree entries for subdirectories that have already been processed
        for (const auto& [subdir, subhash] : treeHashes) {
            // If this 'subdir' is a direct child of the current 'dir'
            if (fs::path(subdir).parent_path().string() == dir) {
                treeContent << "40000 tree " << subhash << " " << fs::path(subdir).filename().string() << "\n"; // Standard mode for trees
            }
        }
        
        std::string treeStr = treeContent.str();
        std::string treeHash = computeHash(treeStr); // Compute hash of the tree content
        storeObject(treeHash, treeStr);             // Store the tree object
        treeHashes[dir] = treeHash;                 // Store the hash for this directory's tree
    }
    
    // The root tree is the one corresponding to the "." (current) directory
    if (treeHashes.count(".")) {
        return treeHashes["."];
    }
    return ""; // No root tree created (e.g., no files)
}


// --- Commit Object Management ---

/**
 * @brief Parses the raw content of a commit object into a CommitObject struct.
 * @param commitHash The SHA-1 hash of the commit object to parse.
 * @return A CommitObject struct populated with parsed data.
 * @throws std::runtime_error if the commit object cannot be read.
 */
CommitObject parseCommitObject(const std::string& commitHash) {
    CommitObject commit;
    commit.hash = commitHash; // Store the commit's own hash
    std::string content = readObject(commitHash); // Read the raw commit object content
    
    std::istringstream ss(content);
    std::string line;

    // Read header lines until an empty line is encountered (which separates header from message)
    while (std::getline(ss, line) && !line.empty()) {
        if (line.rfind("tree ", 0) == 0) { // Starts with "tree "
            commit.tree_hash = line.substr(5, 40); // Extract tree hash (40 chars after "tree ")
        } else if (line.rfind("parent ", 0) == 0) { // Starts with "parent "
            commit.parent_hashes.push_back(line.substr(7, 40)); // Extract parent hash
        } else if (line.rfind("author ", 0) == 0) { // Starts with "author "
            // Extract author name and email, and timestamp
            size_t email_end = line.find_last_of('>');
            if (email_end != std::string::npos) {
                commit.author = line.substr(7, email_end - 6);
                size_t timestamp_start = line.find_first_not_of(' ', email_end + 1);
                if (timestamp_start != std::string::npos) {
                    commit.timestamp = line.substr(timestamp_start); // Timestamp starts after email
                }
            }
        } else if (line.rfind("committer ", 0) == 0) { // Starts with "committer "
            // Extract committer name and email, and timestamp (similar to author)
            size_t email_end = line.find_last_of('>');
            if (email_end != std::string::npos) {
                commit.committer = line.substr(10, email_end - 9);
                size_t timestamp_start = line.find_first_not_of(' ', email_end + 1);
                if (timestamp_start != std::string::npos) {
                    commit.timestamp = line.substr(timestamp_start);
                }
            }
        }
    }

    // If timestamp was not parsed from author/committer lines (e.g., malformed commit),
    // default to current time.
    if (commit.timestamp.empty()) {
        commit.timestamp = std::to_string(std::time(nullptr));
    }

    // The remaining lines in the stringstream are the commit message
    std::string message;
    while (std::getline(ss, line)) {
        message += line + "\n";
    }
    if (!message.empty()) {
        message.pop_back(); // Remove trailing newline
    }
    commit.message = message;

    return commit;
}

/**
 * @brief Constructs the raw content of a CommitObject and stores it.
 * @param commit The CommitObject struct to save.
 * @return The SHA-1 hash of the saved commit object.
 */
std::string saveCommit(const CommitObject& commit) {
    std::stringstream commitContent;
    commitContent << "tree " << commit.tree_hash << "\n"; // Add tree hash line
    
    // Add parent hashes lines (if any)
    for (const auto& parent : commit.parent_hashes) {
        commitContent << "parent " << parent << "\n";
    }
    
    // Get current time for author/committer timestamps
    std::time_t now = std::time(nullptr);
    commitContent << "author " << commit.author << " " << now << "\n";
    commitContent << "committer " << commit.committer << " " << now << "\n\n"; // Two newlines before message
    
    commitContent << commit.message << "\n"; // Add commit message
    
    std::string content = commitContent.str();
    std::string hash = computeHash(content); // Compute hash of the entire commit content
    storeObject(hash, content);            // Store the commit object
  