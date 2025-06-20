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
std::string findCommonAncestor(const std::string& commitHash1, const std::string& commitHash2) {
    // Queues for BFS traversal
    std::queue<std::string> q1, q2;
    // Sets to keep track of visited commits and commits reachable from each branch
    std::unordered_set<std::string> visited1, visited2;
    std::unordered_set<std::string> path1, path2; // Commits on the path from commit1/commit2 back to root

    // Start BFS from both commit hashes
    q1.push(commitHash1);
    visited1.insert(commitHash1);
    path1.insert(commitHash1);

    q2.push(commitHash2);
    visited2.insert(commitHash2);
    path2.insert(commitHash2);

    // Continue as long as there are commits to explore in either queue
    while (!q1.empty() || !q2.empty()) {
        // Process commits from q1
        if (!q1.empty()) {
            std::string current = q1.front();
            q1.pop();

            // If this commit is also reachable from commit2, it's a common ancestor
            if (path2.count(current)) {
                return current; // Found a common ancestor (might not be the lowest, but a valid one)
            }

            // Parse commit to get its parents
            // This assumes parseCommitObject is defined earlier in the file.
            CommitObject commit = parseCommitObject(current);
            for (const std::string& parent : commit.parent_hashes) {
                if (visited1.find(parent) == visited1.end()) { // If parent not yet visited for branch 1
                    visited1.insert(parent);
                    path1.insert(parent);
                    q1.push(parent);
                }
            }
        }

        // Process commits from q2
        if (!q2.empty()) {
            std::string current = q2.front();
            q2.pop();

            // If this commit is also reachable from commit1, it's a common ancestor
            if (path1.count(current)) {
                return current; // Found a common ancestor (might not be the lowest, but a valid one)
            }

            // Parse commit to get its parents
            // This assumes parseCommitObject is defined earlier in the file.
            CommitObject commit = parseCommitObject(current);
            for (const std::string& parent : commit.parent_hashes) {
                if (visited2.find(parent) == visited2.end()) { // If parent not yet visited for branch 2
                    visited2.insert(parent);
                    path2.insert(parent);
                    q2.push(parent);
                }
            }
        }
    }

    // No common ancestor found (e.g., if histories are completely divergent)
    return "";
}


// --- Main Merge Handler Function ---
// This function relies on other MiniGit functions (like getHeadCommitHash, getBranchCommit,
// branchExists, parseCommitObject, getTreeFiles, createTreeFromFiles, saveCommit,
// updateHead, cleanWorkingDirectory, restoreFiles, updateIndex).
// Ensure these functions are defined earlier in your israel core utility.cpp file.
void handleMerge(const std::string& branchName) {
    if (!branchExists(branchName)) {
        std::cerr << "Error: Branch '" << branchName << "' does not exist." << std::endl;
        return;
    }

    std::string currentCommitHash = getHeadCommitHash();
    std::string branchCommitHash = getBranchCommit(branchName);

    if (currentCommitHash.empty()) {
        std::cerr << "Error: No commits on current branch to merge." << std::endl;
        return;
    }

    if (currentCommitHash == branchCommitHash) {
        std::cout << "Already up to date." << std::endl;
        return;
    }

    // Find the lowest common ancestor (LCA)
    std::string commonAncestorHash = findCommonAncestor(currentCommitHash, branchCommitHash);
    
    if (commonAncestorHash.empty()) {
        std::cerr << "Error: Could not find common ancestor between current branch and '" << branchName << "'." << std::endl;
        return;
    }

    // Get all files from LCA, current branch, and target branch
    std::map<fs::path, std::string> ancestorFiles = getTreeFiles(parseCommitObject(commonAncestorHash).tree_hash);
    std::map<fs::path, std::string> currentFiles = getTreeFiles(parseCommitObject(currentCommitHash).tree_hash);
    std::map<fs::path, std::string> branchFiles = getTreeFiles(parseCommitObject(branchCommitHash).tree_hash);

    // Merge changes
    std::set<fs::path> allFiles;
    for (const auto& [path, _] : ancestorFiles) allFiles.insert(path);
    for (const auto& [path, _] : currentFiles) allFiles.insert(path);
    for (const auto& [path, _] : branchFiles) allFiles.insert(path);

    bool hasConflict = false;
    std::map<fs::path, std::string> mergedFiles;

    for (const auto& filePath : allFiles) {
        std::string ancestorHash = ancestorFiles.count(filePath) ? ancestorFiles.at(filePath) : "";
        std::string currentHash = currentFiles.count(filePath) ? currentFiles.at(filePath) : "";
        std::string branchHash = branchFiles.count(filePath) ? branchFiles.at(filePath) : "";

        if (ancestorHash == currentHash && ancestorHash == branchHash) {
            // No changes in any branch from ancestor
            if(currentFiles.count(filePath)) mergedFiles[filePath] = currentHash; // If file existed, keep it
            // else: file was deleted in all, so don't add to mergedFiles
        } else if (ancestorHash == currentHash && ancestorHash != branchHash) {
            // Changed in branch, not in current (or deleted in branch, not in current)
            if(branchFiles.count(filePath)) { // If file exists in branch, take branch version
                mergedFiles[filePath] = branchHash;
            } else { // File was deleted in branch
                // Don't add to mergedFiles (effectively delete)
            }
        } else if (ancestorHash != currentHash && ancestorHash == branchHash) {
            // Changed in current, not in branch (or deleted in current, not in branch)
            if(currentFiles.count(filePath)) { // If file exists in current, take current version
                mergedFiles[filePath] = currentHash;
            } else { // File was deleted in current
                // Don't add to mergedFiles (effectively delete)
            }
        } else if (currentHash == branchHash) {
            // Same change in both (or same deletion in both)
            if(currentFiles.count(filePath)) mergedFiles[filePath] = currentHash;
        } else {
            // Conflict: changed differently in both OR one deleted and other modified.
            std::cerr << "CONFLICT: Both modified " << filePath.string() << std::endl;
            hasConflict = true;
            // For a simple MiniGit, we will take the current version in case of conflict.
            // A more robust Git would write conflict markers (<<<<<<<, =======, >>>>>>>)
            // into the file in the working directory and mark it as conflicted in the index.
            if(currentFiles.count(filePath)) mergedFiles[filePath] = currentHash;
            // If deleted in one and modified in other, this simplified merge takes the modified version.
        }
    }

    // If there were conflicts, we don't auto-commit. We update the working directory
    // and staging area with our simplified merge, and let the user resolve.
    if (hasConflict) {
        std::cerr << "Merge failed due to conflicts - please resolve them manually." << std::endl;
        // Update working directory and staging area to the state after the simplified merge
        cleanWorkingDirectory(getPaths(mergedFiles));
        restoreFiles(mergedFiles);
        staging_area = mergedFiles; // Staging area now reflects the (potentially conflicted) merge result
        updateIndex();
        return; // Exit without creating a merge commit
    }

    // Create a merge commit
    // This assumes createTreeFromFiles and saveCommit are defined earlier.
    CommitObject newCommit;
    newCommit.tree_hash = createTreeFromFiles(mergedFiles);
    newCommit.message = "Merge branch '" + branchName + "' into " + current_branch;
    newCommit.author = "User <user@example.com>";
    newCommit.committer = "User <user@example.com>";
    newCommit.parent_hashes.push_back(currentCommitHash); // Parent 1: current branch HEAD
    newCommit.parent_hashes.push_back(branchCommitHash); // Parent 2: merged branch HEAD

    std::string commitHash = saveCommit(newCommit);
    updateHead(commitHash, current_branch); // Update HEAD and the current branch pointer

    // Update working directory and staging area to reflect the successful merge
    cleanWorkingDirectory(getPaths(mergedFiles));
    restoreFiles(mergedFiles);
    staging_area = mergedFiles; // Update staging area to reflect the merge
    updateIndex();

    std::cout << "Successfully merged '" << branchName << "' into '" << current_branch << "'." << std::endl;
}

// --- Main Function (with merge command added) ---
// This assumes handleInit, handleAdd, handleCommit, handleLog,
// handleBranch, handleCheckout are defined earlier in the file.
// Also assumes standard includes like <iostream>, <vector>, etc., are at the top.
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <command> [args...]" << std::endl;
        return 1;
    }

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    std::string command = args[0];

    try {
        if (command == "init") {
            handleInit();
        } else if (command == "add") {
            if (args.size() < 2) {
                std::cerr << "Usage: minigit add <file1> [file2...]" << std::endl;
                return 1;
            }
            std::vector<std::string> files(args.begin() + 1, args.end());
            handleAdd(files);
        } else if (command == "commit") {
            if (args.size() != 3 || args[1] != "-m") {
                std::cerr << "Usage: minigit commit -m \"<message>\"" << std::endl;
                return 1;
            }
            handleCommit(args[2]);
        } else if (command == "log") {
            handleLog();
        } else if (command == "branch") {
            if (args.size() == 1) {
                handleBranch("");
            } else if (args.size() == 2) {
                handleBranch(args[1]);
            } else {
                std::cerr << "Usage: minigit branch OR minigit branch <name>" << std::endl;
                return 1;
            }
        } else if (command == "checkout") {
            if (args.size() != 2) {
                std::cerr << "Usage: minigit checkout <branch_name_or_commit_hash>" << std::endl;
                return 1;
            }
            handleCheckout(args[1]);
        } else if (command == "merge") { // Added merge command handler
            if (args.size() != 2) {
                std::cerr << "Usage: minigit merge <branch_name>" << std::endl;
                return 1;
            }
            handleMerge(args[1]);
        } 
        else {
            std::cerr << "Unknown command: " << command << std::endl;
            std::cerr << "Available commands: init, add, commit, log, branch, checkout, merge" << std::endl; // Updated for merge
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
