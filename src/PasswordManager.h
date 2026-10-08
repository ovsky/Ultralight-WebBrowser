#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <mutex>
#include <functional>
#include <filesystem>

/**
 * Password Manager - Chrome-like password save and autofill functionality
 *
 * Features:
 * - Secure storage with encryption
 * - Form detection and credential capture
 * - Auto-fill support
 * - Password generation
 * - Import/Export capabilities
 * - Master password protection (optional)
 */

namespace password
{

    // Represents a saved credential
    struct SavedCredential
    {
        std::string id;                  // Unique identifier (UUID)
        std::string origin;              // Website origin (e.g., https://example.com)
        std::string signon_realm;        // Full login realm URL
        std::string username;            // Username/email
        std::string encrypted_password;  // Encrypted password (stored)
        std::string password;            // Decrypted password (in memory only)
        std::string username_field;      // Form field name for username
        std::string password_field;      // Form field name for password
        std::string form_action;         // Form submission URL
        uint64_t date_created;           // Timestamp when created
        uint64_t date_last_used;         // Timestamp when last used
        uint64_t date_password_modified; // Timestamp when password changed
        uint32_t times_used;             // Number of times used for autofill
        bool blacklisted;                // True if user chose "Never save" for this site
        std::string notes;               // Optional user notes

        SavedCredential();
        bool IsValid() const;
        std::string GetDisplayName() const;
    };

    // Represents a detected login form on a page
    struct DetectedForm
    {
        std::string origin;
        std::string action_url;
        std::string username_field_name;
        std::string username_field_id;
        std::string password_field_name;
        std::string password_field_id;
        std::string username_value;
        std::string password_value;
        std::string form_id;
        bool has_remember_me;
        bool is_signup_form;          // Detected as registration form
        bool is_change_password_form; // Detected as password change form

        DetectedForm();
        bool IsLoginForm() const;
        std::string GetFormKey() const;
    };

    // Password generation options
    struct PasswordGeneratorOptions
    {
        int length = 16;
        bool include_uppercase = true;
        bool include_lowercase = true;
        bool include_numbers = true;
        bool include_symbols = true;
        std::string excluded_chars; // Characters to exclude

        std::string GeneratePassword() const;
    };

    // Password strength result
    enum class PasswordStrength
    {
        VeryWeak = 0,
        Weak = 1,
        Fair = 2,
        Strong = 3,
        VeryStrong = 4
    };

    struct PasswordStrengthResult
    {
        PasswordStrength strength;
        int score; // 0-100
        std::string feedback;
        std::vector<std::string> suggestions;
    };

    // Callback types
    using SavePromptCallback = std::function<void(bool save, bool never_for_site)>;
    using UpdatePromptCallback = std::function<void(bool update)>;
    using CredentialSelectedCallback = std::function<void(const std::string &username, const std::string &password)>;

    // Main Password Manager class
    class PasswordManager
    {
    public:
        PasswordManager();
        ~PasswordManager();

        // Initialization
        bool Initialize(const std::filesystem::path &data_directory);
        void Shutdown();

        // Core password operations
        bool SaveCredential(const SavedCredential &credential);
        bool UpdateCredential(const SavedCredential &credential);
        bool DeleteCredential(const std::string &id);
        bool DeleteCredentialsForOrigin(const std::string &origin);

        // Lookup operations
        std::vector<SavedCredential> GetCredentialsForOrigin(const std::string &origin) const;
        std::vector<SavedCredential> GetAllCredentials() const;
        SavedCredential *FindCredential(const std::string &id);
        const SavedCredential *FindCredential(const std::string &id) const;
        bool HasCredentialsForOrigin(const std::string &origin) const;

        // Blacklist management (sites where user chose "Never save")
        bool IsOriginBlacklisted(const std::string &origin) const;
        void BlacklistOrigin(const std::string &origin);
        void RemoveFromBlacklist(const std::string &origin);
        std::vector<std::string> GetBlacklistedOrigins() const;

        // Form detection and submission handling
        void OnFormDetected(const DetectedForm &form);
        void OnFormSubmitted(const DetectedForm &form);
        void OnLoginSuccessful(const std::string &origin);
        void OnLoginFailed(const std::string &origin);

        // Autofill support
        bool ShouldOfferAutofill(const std::string &origin) const;
        std::vector<SavedCredential> GetAutofillSuggestions(const std::string &origin,
                                                            const std::string &username_hint = "") const;
        void RecordAutofillUsage(const std::string &credential_id);

        // Password generation
        std::string GeneratePassword(const PasswordGeneratorOptions &options = PasswordGeneratorOptions());
        PasswordStrengthResult CheckPasswordStrength(const std::string &password) const;

        // Import/Export
        bool ExportToCSV(const std::filesystem::path &filepath, const std::string &master_password = "") const;
        bool ImportFromCSV(const std::filesystem::path &filepath);
        bool ExportToJSON(const std::filesystem::path &filepath) const;
        bool ImportFromJSON(const std::filesystem::path &filepath);

        // Master password (optional additional security)
        bool SetMasterPassword(const std::string &password);
        bool VerifyMasterPassword(const std::string &password) const;
        bool HasMasterPassword() const;
        bool RemoveMasterPassword(const std::string &current_password);
        bool ChangeMasterPassword(const std::string &old_password, const std::string &new_password);
        bool IsLocked() const;
        bool Unlock(const std::string &master_password);
        void Lock();

        // Settings
        struct Settings
        {
            bool offer_to_save_passwords = true;
            bool auto_signin = true;
            bool check_passwords_leaked = false; // Future: integration with breach databases
            bool generate_passwords_automatically = true;
            int auto_lock_timeout_minutes = 15; // 0 = never auto-lock
            bool require_master_password = false;
        };

        Settings &GetSettings();
        const Settings &GetSettings() const;
        void SaveSettings();

        // Statistics
        struct Stats
        {
            size_t total_passwords;
            size_t weak_passwords;
            size_t reused_passwords;
            size_t old_passwords; // Not changed in 90+ days
            size_t blacklisted_sites;
        };

        Stats GetStats() const;

        // Persistence
        bool Load();
        bool Save();

        // Event callbacks
        void SetSavePromptCallback(SavePromptCallback callback);
        void SetUpdatePromptCallback(UpdatePromptCallback callback);
        void SetCredentialSelectedCallback(CredentialSelectedCallback callback);

        // Utility
        static std::string ExtractOriginFromURL(const std::string &url);
        static std::string GenerateUUID();
        static uint64_t GetCurrentTimestamp();

    private:
        // Encryption helpers
        std::string Encrypt(const std::string &plaintext) const;
        std::string Decrypt(const std::string &ciphertext) const;
        std::string HashMasterPassword(const std::string &password) const;
        std::string DeriveKey(const std::string &password) const;

        // Internal helpers
        void LoadBlacklist();
        void SaveBlacklist();
        void LoadSettings();
        bool ShouldPromptToSave(const DetectedForm &form) const;
        bool IsNewCredential(const DetectedForm &form) const;
        bool IsUpdatedCredential(const DetectedForm &form, const SavedCredential &existing) const;

        // Platform-specific encryption key management
        std::string GetEncryptionKey() const;
        void RegenerateEncryptionKey();

        // Member variables
        std::filesystem::path data_dir_;
        std::filesystem::path passwords_file_;
        std::filesystem::path blacklist_file_;
        std::filesystem::path settings_file_;

        std::vector<SavedCredential> credentials_;
        std::vector<std::string> blacklisted_origins_;
        Settings settings_;

        std::string encryption_key_;
        std::string master_password_hash_;
        bool is_locked_;
        std::chrono::steady_clock::time_point last_activity_;

        mutable std::mutex mutex_;

        // Pending form submissions awaiting user decision
        std::map<std::string, DetectedForm> pending_forms_;

        // Callbacks
        SavePromptCallback save_prompt_callback_;
        UpdatePromptCallback update_prompt_callback_;
        CredentialSelectedCallback credential_selected_callback_;

        bool initialized_;
    };

    // Global password manager instance
    PasswordManager &GetPasswordManager();

} // namespace password
