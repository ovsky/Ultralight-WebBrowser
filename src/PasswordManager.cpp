#include "PasswordManager.h"
#include "Utils.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <random>
#include <iomanip>
#include <cstring>
#include <cctype>
#include <regex>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#include <dpapi.h>
#pragma comment(lib, "crypt32.lib")
#elif defined(__APPLE__)
#include <Security/Security.h>
#include <CommonCrypto/CommonCrypto.h>
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/aes.h>
#endif

namespace password
{

    namespace
    {

        // Simple Base64 encoding/decoding
        static const std::string base64_chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/";

        std::string Base64Encode(const std::string &input)
        {
            std::string ret;
            int i = 0;
            int j = 0;
            unsigned char char_array_3[3];
            unsigned char char_array_4[4];
            size_t in_len = input.size();
            const unsigned char *bytes_to_encode = reinterpret_cast<const unsigned char *>(input.data());

            while (in_len--)
            {
                char_array_3[i++] = *(bytes_to_encode++);
                if (i == 3)
                {
                    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                    char_array_4[3] = char_array_3[2] & 0x3f;

                    for (i = 0; i < 4; i++)
                        ret += base64_chars[char_array_4[i]];
                    i = 0;
                }
            }

            if (i)
            {
                for (j = i; j < 3; j++)
                    char_array_3[j] = '\0';

                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

                for (j = 0; j < i + 1; j++)
                    ret += base64_chars[char_array_4[j]];

                while (i++ < 3)
                    ret += '=';
            }

            return ret;
        }

        std::string Base64Decode(const std::string &encoded_string)
        {
            size_t in_len = encoded_string.size();
            int i = 0;
            int j = 0;
            int in_ = 0;
            unsigned char char_array_4[4], char_array_3[3];
            std::string ret;

            while (in_len-- && encoded_string[in_] != '=' &&
                   (isalnum(encoded_string[in_]) || encoded_string[in_] == '+' || encoded_string[in_] == '/'))
            {
                char_array_4[i++] = encoded_string[in_];
                in_++;
                if (i == 4)
                {
                    for (i = 0; i < 4; i++)
                        char_array_4[i] = static_cast<unsigned char>(base64_chars.find(char_array_4[i]));

                    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
                    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
                    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

                    for (i = 0; i < 3; i++)
                        ret += char_array_3[i];
                    i = 0;
                }
            }

            if (i)
            {
                for (j = 0; j < i; j++)
                    char_array_4[j] = static_cast<unsigned char>(base64_chars.find(char_array_4[j]));

                char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
                char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);

                for (j = 0; j < i - 1; j++)
                    ret += char_array_3[j];
            }

            return ret;
        }

        // Simple JSON parsing helpers
        std::string ParseJsonString(const std::string &json, const std::string &key)
        {
            std::string search = "\"" + key + "\"";
            auto pos = json.find(search);
            if (pos == std::string::npos)
                return "";

            pos = json.find(':', pos);
            if (pos == std::string::npos)
                return "";

            pos = json.find('"', pos);
            if (pos == std::string::npos)
                return "";
            pos++;

            std::string result;
            while (pos < json.size() && json[pos] != '"')
            {
                if (json[pos] == '\\' && pos + 1 < json.size())
                {
                    pos++;
                    switch (json[pos])
                    {
                    case 'n':
                        result += '\n';
                        break;
                    case 'r':
                        result += '\r';
                        break;
                    case 't':
                        result += '\t';
                        break;
                    case '"':
                        result += '"';
                        break;
                    case '\\':
                        result += '\\';
                        break;
                    default:
                        result += json[pos];
                        break;
                    }
                }
                else
                {
                    result += json[pos];
                }
                pos++;
            }
            return result;
        }

        uint64_t ParseJsonUint64(const std::string &json, const std::string &key)
        {
            std::string search = "\"" + key + "\"";
            auto pos = json.find(search);
            if (pos == std::string::npos)
                return 0;

            pos = json.find(':', pos);
            if (pos == std::string::npos)
                return 0;
            pos++;

            while (pos < json.size() && std::isspace(json[pos]))
                pos++;

            std::string num;
            while (pos < json.size() && std::isdigit(json[pos]))
            {
                num += json[pos++];
            }

            return num.empty() ? 0 : std::stoull(num);
        }

        bool ParseJsonBool(const std::string &json, const std::string &key, bool default_val = false)
        {
            std::string search = "\"" + key + "\"";
            auto pos = json.find(search);
            if (pos == std::string::npos)
                return default_val;

            pos = json.find(':', pos);
            if (pos == std::string::npos)
                return default_val;
            pos++;

            while (pos < json.size() && std::isspace(json[pos]))
                pos++;

            if (json.compare(pos, 4, "true") == 0)
                return true;
            if (json.compare(pos, 5, "false") == 0)
                return false;
            return default_val;
        }

        std::string EscapeJsonStr(const std::string &s)
        {
            std::string result;
            for (char c : s)
            {
                switch (c)
                {
                case '"':
                    result += "\\\"";
                    break;
                case '\\':
                    result += "\\\\";
                    break;
                case '\n':
                    result += "\\n";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                case '\t':
                    result += "\\t";
                    break;
                default:
                    result += c;
                    break;
                }
            }
            return result;
        }

    } // anonymous namespace

    // SavedCredential implementation
    SavedCredential::SavedCredential()
        : date_created(0), date_last_used(0), date_password_modified(0), times_used(0), blacklisted(false)
    {
    }

    bool SavedCredential::IsValid() const
    {
        return !origin.empty() && !username.empty() && (!password.empty() || !encrypted_password.empty());
    }

    std::string SavedCredential::GetDisplayName() const
    {
        if (!username.empty())
            return username;
        return origin;
    }

    // DetectedForm implementation
    DetectedForm::DetectedForm()
        : has_remember_me(false), is_signup_form(false), is_change_password_form(false)
    {
    }

    bool DetectedForm::IsLoginForm() const
    {
        return !is_signup_form && !is_change_password_form &&
               !username_value.empty() && !password_value.empty();
    }

    std::string DetectedForm::GetFormKey() const
    {
        return origin + "|" + action_url + "|" + username_field_name;
    }

    // PasswordGeneratorOptions implementation
    std::string PasswordGeneratorOptions::GeneratePassword() const
    {
        std::string chars;

        if (include_lowercase)
            chars += "abcdefghijklmnopqrstuvwxyz";
        if (include_uppercase)
            chars += "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        if (include_numbers)
            chars += "0123456789";
        if (include_symbols)
            chars += "!@#$%^&*()_+-=[]{}|;:,.<>?";

        // Remove excluded characters
        for (char c : excluded_chars)
        {
            chars.erase(std::remove(chars.begin(), chars.end(), c), chars.end());
        }

        if (chars.empty())
        {
            chars = "abcdefghijklmnopqrstuvwxyz";
        }

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, static_cast<int>(chars.size()) - 1);

        std::string password;
        password.reserve(length);

        for (int i = 0; i < length; i++)
        {
            password += chars[dis(gen)];
        }

        return password;
    }

    // PasswordManager implementation
    PasswordManager::PasswordManager()
        : is_locked_(false), initialized_(false)
    {
    }

    PasswordManager::~PasswordManager()
    {
        Shutdown();
    }

    bool PasswordManager::Initialize(const std::filesystem::path &data_directory)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (initialized_)
            return true;

        data_dir_ = data_directory / "passwords";

        try
        {
            std::filesystem::create_directories(data_dir_);
        }
        catch (...)
        {
            return false;
        }

        passwords_file_ = data_dir_ / "credentials.dat";
        blacklist_file_ = data_dir_ / "blacklist.json";
        settings_file_ = data_dir_ / "settings.json";

        // Initialize encryption key
        encryption_key_ = GetEncryptionKey();

        LoadSettings();
        LoadBlacklist();

        if (!Load())
        {
            // Start with empty credentials if file doesn't exist
            credentials_.clear();
        }

        last_activity_ = std::chrono::steady_clock::now();
        initialized_ = true;

        return true;
    }

    void PasswordManager::Shutdown()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_)
            return;

        Save();
        SaveBlacklist();
        SaveSettings();

        // Clear sensitive data
        for (auto &cred : credentials_)
        {
            std::fill(cred.password.begin(), cred.password.end(), '\0');
            cred.password.clear();
        }

        std::fill(encryption_key_.begin(), encryption_key_.end(), '\0');
        encryption_key_.clear();

        initialized_ = false;
    }

    bool PasswordManager::SaveCredential(const SavedCredential &credential)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_ || !credential.IsValid())
            return false;

        SavedCredential cred = credential;

        // Generate ID if not set
        if (cred.id.empty())
        {
            cred.id = GenerateUUID();
        }

        // Set timestamps
        uint64_t now = GetCurrentTimestamp();
        if (cred.date_created == 0)
        {
            cred.date_created = now;
        }
        cred.date_password_modified = now;

        // Encrypt password
        if (!cred.password.empty())
        {
            cred.encrypted_password = Encrypt(cred.password);
        }

        // Check for existing credential with same origin+username
        auto it = std::find_if(credentials_.begin(), credentials_.end(),
                               [&cred](const SavedCredential &c)
                               {
                                   return c.origin == cred.origin && c.username == cred.username;
                               });

        if (it != credentials_.end())
        {
            // Update existing
            cred.date_created = it->date_created;
            cred.times_used = it->times_used;
            *it = cred;
        }
        else
        {
            credentials_.push_back(cred);
        }

        // Remove from blacklist if it was there
        auto bl_it = std::find(blacklisted_origins_.begin(), blacklisted_origins_.end(), cred.origin);
        if (bl_it != blacklisted_origins_.end())
        {
            blacklisted_origins_.erase(bl_it);
            SaveBlacklist();
        }

        return Save();
    }

    bool PasswordManager::UpdateCredential(const SavedCredential &credential)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_)
            return false;

        auto it = std::find_if(credentials_.begin(), credentials_.end(),
                               [&credential](const SavedCredential &c)
                               { return c.id == credential.id; });

        if (it == credentials_.end())
            return false;

        SavedCredential updated = credential;
        updated.date_password_modified = GetCurrentTimestamp();

        if (!updated.password.empty() && updated.password != it->password)
        {
            updated.encrypted_password = Encrypt(updated.password);
        }

        *it = updated;
        return Save();
    }

    bool PasswordManager::DeleteCredential(const std::string &id)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_)
            return false;

        auto it = std::find_if(credentials_.begin(), credentials_.end(),
                               [&id](const SavedCredential &c)
                               { return c.id == id; });

        if (it == credentials_.end())
            return false;

        // Clear sensitive data
        std::fill(it->password.begin(), it->password.end(), '\0');

        credentials_.erase(it);
        return Save();
    }

    bool PasswordManager::DeleteCredentialsForOrigin(const std::string &origin)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_)
            return false;

        auto it = std::remove_if(credentials_.begin(), credentials_.end(),
                                 [&origin](SavedCredential &c)
                                 {
                                     if (c.origin == origin)
                                     {
                                         std::fill(c.password.begin(), c.password.end(), '\0');
                                         return true;
                                     }
                                     return false;
                                 });

        if (it == credentials_.end())
            return false;

        credentials_.erase(it, credentials_.end());
        return Save();
    }

    std::vector<SavedCredential> PasswordManager::GetCredentialsForOrigin(const std::string &origin) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<SavedCredential> result;

        if (!initialized_)
            return result;

        std::string normalized_origin = ExtractOriginFromURL(origin);

        for (const auto &cred : credentials_)
        {
            if (cred.origin == normalized_origin && !cred.blacklisted)
            {
                SavedCredential decrypted = cred;
                if (!decrypted.encrypted_password.empty() && decrypted.password.empty())
                {
                    decrypted.password = Decrypt(decrypted.encrypted_password);
                }
                result.push_back(decrypted);
            }
        }

        // Sort by times_used (most used first), then by date_last_used
        std::sort(result.begin(), result.end(),
                  [](const SavedCredential &a, const SavedCredential &b)
                  {
                      if (a.times_used != b.times_used)
                          return a.times_used > b.times_used;
                      return a.date_last_used > b.date_last_used;
                  });

        return result;
    }

    std::vector<SavedCredential> PasswordManager::GetAllCredentials() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<SavedCredential> result;

        if (!initialized_)
            return result;

        for (const auto &cred : credentials_)
        {
            SavedCredential decrypted = cred;
            if (!decrypted.encrypted_password.empty() && decrypted.password.empty())
            {
                decrypted.password = Decrypt(decrypted.encrypted_password);
            }
            result.push_back(decrypted);
        }

        return result;
    }

    SavedCredential *PasswordManager::FindCredential(const std::string &id)
    {
        for (auto &cred : credentials_)
        {
            if (cred.id == id)
                return &cred;
        }
        return nullptr;
    }

    const SavedCredential *PasswordManager::FindCredential(const std::string &id) const
    {
        for (const auto &cred : credentials_)
        {
            if (cred.id == id)
                return &cred;
        }
        return nullptr;
    }

    bool PasswordManager::HasCredentialsForOrigin(const std::string &origin) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::string normalized = ExtractOriginFromURL(origin);

        return std::any_of(credentials_.begin(), credentials_.end(),
                           [&normalized](const SavedCredential &c)
                           {
                               return c.origin == normalized && !c.blacklisted;
                           });
    }

    bool PasswordManager::IsOriginBlacklisted(const std::string &origin) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::string normalized = ExtractOriginFromURL(origin);

        return std::find(blacklisted_origins_.begin(), blacklisted_origins_.end(), normalized) != blacklisted_origins_.end();
    }

    void PasswordManager::BlacklistOrigin(const std::string &origin)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::string normalized = ExtractOriginFromURL(origin);

        if (std::find(blacklisted_origins_.begin(), blacklisted_origins_.end(), normalized) == blacklisted_origins_.end())
        {
            blacklisted_origins_.push_back(normalized);
            SaveBlacklist();
        }
    }

    void PasswordManager::RemoveFromBlacklist(const std::string &origin)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::string normalized = ExtractOriginFromURL(origin);

        auto it = std::find(blacklisted_origins_.begin(), blacklisted_origins_.end(), normalized);
        if (it != blacklisted_origins_.end())
        {
            blacklisted_origins_.erase(it);
            SaveBlacklist();
        }
    }

    std::vector<std::string> PasswordManager::GetBlacklistedOrigins() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return blacklisted_origins_;
    }

    void PasswordManager::OnFormDetected(const DetectedForm &form)
    {
        // Store detected form for potential later use
        std::lock_guard<std::mutex> lock(mutex_);
        pending_forms_[form.GetFormKey()] = form;
    }

    void PasswordManager::OnFormSubmitted(const DetectedForm &form)
    {
        if (!settings_.offer_to_save_passwords)
            return;
        if (form.username_value.empty() || form.password_value.empty())
            return;
        if (IsOriginBlacklisted(form.origin))
            return;

        std::string origin = ExtractOriginFromURL(form.origin);

        // Check if this is a new credential or an update
        auto existing = GetCredentialsForOrigin(origin);

        bool found_exact_match = false;
        bool found_username_match = false;
        SavedCredential matched_cred;

        for (const auto &cred : existing)
        {
            if (cred.username == form.username_value)
            {
                found_username_match = true;
                matched_cred = cred;
                if (cred.password == form.password_value)
                {
                    found_exact_match = true;
                    break;
                }
            }
        }

        if (found_exact_match)
        {
            // Credentials already saved and match - just update usage
            RecordAutofillUsage(matched_cred.id);
            return;
        }

        if (found_username_match)
        {
            // Password changed - store pending form for later update when user responds
            std::string form_key = origin + "|" + form.username_value;
            pending_forms_[form_key] = form;

            // Notify UI to show update prompt (the callback will be called with user's response)
            // For now, auto-update if callback not set
            if (!update_prompt_callback_)
            {
                SavedCredential updated = matched_cred;
                updated.password = form.password_value;
                UpdateCredential(updated);
            }
        }
        else
        {
            // New credential - store pending form for later save when user responds
            std::string form_key = origin + "|" + form.username_value;
            pending_forms_[form_key] = form;

            // Notify UI to show save prompt (the callback will be called with user's response)
            // For now, auto-save if callback not set
            if (!save_prompt_callback_)
            {
                SavedCredential cred;
                cred.origin = origin;
                cred.signon_realm = form.action_url.empty() ? origin : form.action_url;
                cred.username = form.username_value;
                cred.password = form.password_value;
                cred.username_field = form.username_field_name;
                cred.password_field = form.password_field_name;
                cred.form_action = form.action_url;
                SaveCredential(cred);
            }
        }
    }

    void PasswordManager::OnLoginSuccessful(const std::string &origin)
    {
        // Could be used to confirm that a recently-submitted credential worked
        (void)origin;
    }

    void PasswordManager::OnLoginFailed(const std::string &origin)
    {
        // Could be used to detect if a saved password is no longer valid
        (void)origin;
    }

    bool PasswordManager::ShouldOfferAutofill(const std::string &origin) const
    {
        if (!settings_.auto_signin)
            return false;
        if (IsOriginBlacklisted(origin))
            return false;
        return HasCredentialsForOrigin(origin);
    }

    std::vector<SavedCredential> PasswordManager::GetAutofillSuggestions(
        const std::string &origin, const std::string &username_hint) const
    {

        auto creds = GetCredentialsForOrigin(origin);

        if (!username_hint.empty())
        {
            std::string hint_lower = username_hint;
            std::transform(hint_lower.begin(), hint_lower.end(), hint_lower.begin(), ::tolower);

            std::vector<SavedCredential> filtered;
            for (const auto &cred : creds)
            {
                std::string username_lower = cred.username;
                std::transform(username_lower.begin(), username_lower.end(), username_lower.begin(), ::tolower);

                if (username_lower.find(hint_lower) == 0)
                {
                    filtered.push_back(cred);
                }
            }
            return filtered;
        }

        return creds;
    }

    void PasswordManager::RecordAutofillUsage(const std::string &credential_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto cred = FindCredential(credential_id);
        if (cred)
        {
            cred->times_used++;
            cred->date_last_used = GetCurrentTimestamp();
            Save();
        }
    }

    std::string PasswordManager::GeneratePassword(const PasswordGeneratorOptions &options)
    {
        return options.GeneratePassword();
    }

    PasswordStrengthResult PasswordManager::CheckPasswordStrength(const std::string &password) const
    {
        PasswordStrengthResult result;
        result.score = 0;

        if (password.empty())
        {
            result.strength = PasswordStrength::VeryWeak;
            result.feedback = "Password is empty";
            return result;
        }

        // Length score
        int length = static_cast<int>(password.length());
        if (length >= 16)
            result.score += 30;
        else if (length >= 12)
            result.score += 25;
        else if (length >= 8)
            result.score += 15;
        else
            result.suggestions.push_back("Use at least 8 characters");

        // Character variety
        bool has_lower = false, has_upper = false, has_digit = false, has_special = false;
        for (char c : password)
        {
            if (std::islower(c))
                has_lower = true;
            else if (std::isupper(c))
                has_upper = true;
            else if (std::isdigit(c))
                has_digit = true;
            else
                has_special = true;
        }

        int variety_count = (has_lower ? 1 : 0) + (has_upper ? 1 : 0) +
                            (has_digit ? 1 : 0) + (has_special ? 1 : 0);

        result.score += variety_count * 15;

        if (!has_lower)
            result.suggestions.push_back("Add lowercase letters");
        if (!has_upper)
            result.suggestions.push_back("Add uppercase letters");
        if (!has_digit)
            result.suggestions.push_back("Add numbers");
        if (!has_special)
            result.suggestions.push_back("Add special characters");

        // Check for common patterns (simplified)
        std::string lower_pass = password;
        std::transform(lower_pass.begin(), lower_pass.end(), lower_pass.begin(), ::tolower);

        std::vector<std::string> common_patterns = {
            "password", "123456", "qwerty", "abc123", "letmein", "welcome",
            "admin", "login", "pass", "1234"};

        for (const auto &pattern : common_patterns)
        {
            if (lower_pass.find(pattern) != std::string::npos)
            {
                result.score -= 20;
                result.suggestions.push_back("Avoid common words and patterns");
                break;
            }
        }

        // Repeated characters penalty
        int repeats = 0;
        for (size_t i = 1; i < password.length(); i++)
        {
            if (password[i] == password[i - 1])
                repeats++;
        }
        if (repeats > 2)
        {
            result.score -= 10;
            result.suggestions.push_back("Avoid repeated characters");
        }

        // Ensure score is in range
        result.score = std::max(0, std::min(100, result.score));

        // Determine strength level
        if (result.score >= 80)
        {
            result.strength = PasswordStrength::VeryStrong;
            result.feedback = "Very strong password";
        }
        else if (result.score >= 60)
        {
            result.strength = PasswordStrength::Strong;
            result.feedback = "Strong password";
        }
        else if (result.score >= 40)
        {
            result.strength = PasswordStrength::Fair;
            result.feedback = "Fair password - could be stronger";
        }
        else if (result.score >= 20)
        {
            result.strength = PasswordStrength::Weak;
            result.feedback = "Weak password - please improve";
        }
        else
        {
            result.strength = PasswordStrength::VeryWeak;
            result.feedback = "Very weak password";
        }

        return result;
    }

    bool PasswordManager::ExportToCSV(const std::filesystem::path &filepath, const std::string &master_password) const
    {
        (void)master_password; // Could be used to encrypt the export

        std::lock_guard<std::mutex> lock(mutex_);

        std::ofstream file(filepath);
        if (!file.is_open())
            return false;

        // Write header (Chrome format)
        file << "name,url,username,password,note\n";

        for (const auto &cred : credentials_)
        {
            if (cred.blacklisted)
                continue;

            std::string password = cred.password;
            if (password.empty() && !cred.encrypted_password.empty())
            {
                password = Decrypt(cred.encrypted_password);
            }

            // Escape fields for CSV
            auto escape_csv = [](const std::string &s)
            {
                if (s.find_first_of(",\"\n\r") != std::string::npos)
                {
                    std::string escaped = "\"";
                    for (char c : s)
                    {
                        if (c == '"')
                            escaped += "\"\"";
                        else
                            escaped += c;
                    }
                    escaped += "\"";
                    return escaped;
                }
                return s;
            };

            file << escape_csv(cred.GetDisplayName()) << ","
                 << escape_csv(cred.origin) << ","
                 << escape_csv(cred.username) << ","
                 << escape_csv(password) << ","
                 << escape_csv(cred.notes) << "\n";
        }

        return true;
    }

    bool PasswordManager::ImportFromCSV(const std::filesystem::path &filepath)
    {
        std::ifstream file(filepath);
        if (!file.is_open())
            return false;

        std::string line;
        bool first_line = true;
        int imported = 0;

        while (std::getline(file, line))
        {
            if (first_line)
            {
                first_line = false;
                // Skip header
                continue;
            }

            if (line.empty())
                continue;

            // Simple CSV parsing (doesn't handle all edge cases)
            std::vector<std::string> fields;
            std::string current;
            bool in_quotes = false;

            for (size_t i = 0; i < line.size(); i++)
            {
                char c = line[i];
                if (c == '"')
                {
                    if (in_quotes && i + 1 < line.size() && line[i + 1] == '"')
                    {
                        current += '"';
                        i++;
                    }
                    else
                    {
                        in_quotes = !in_quotes;
                    }
                }
                else if (c == ',' && !in_quotes)
                {
                    fields.push_back(current);
                    current.clear();
                }
                else
                {
                    current += c;
                }
            }
            fields.push_back(current);

            if (fields.size() >= 4)
            {
                SavedCredential cred;
                cred.origin = ExtractOriginFromURL(fields.size() > 1 ? fields[1] : "");
                cred.signon_realm = cred.origin;
                cred.username = fields.size() > 2 ? fields[2] : "";
                cred.password = fields.size() > 3 ? fields[3] : "";
                cred.notes = fields.size() > 4 ? fields[4] : "";

                if (cred.IsValid())
                {
                    SaveCredential(cred);
                    imported++;
                }
            }
        }

        return imported > 0;
    }

    bool PasswordManager::ExportToJSON(const std::filesystem::path &filepath) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::ofstream file(filepath);
        if (!file.is_open())
            return false;

        file << "{\n  \"credentials\": [\n";

        bool first = true;
        for (const auto &cred : credentials_)
        {
            if (cred.blacklisted)
                continue;

            if (!first)
                file << ",\n";
            first = false;

            std::string password = cred.password;
            if (password.empty() && !cred.encrypted_password.empty())
            {
                password = Decrypt(cred.encrypted_password);
            }

            file << "    {\n"
                 << "      \"origin\": \"" << EscapeJsonStr(cred.origin) << "\",\n"
                 << "      \"username\": \"" << EscapeJsonStr(cred.username) << "\",\n"
                 << "      \"password\": \"" << EscapeJsonStr(password) << "\",\n"
                 << "      \"notes\": \"" << EscapeJsonStr(cred.notes) << "\"\n"
                 << "    }";
        }

        file << "\n  ]\n}\n";

        return true;
    }

    bool PasswordManager::ImportFromJSON(const std::filesystem::path &filepath)
    {
        std::ifstream file(filepath);
        if (!file.is_open())
            return false;

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        // Very simple JSON array parsing
        size_t pos = 0;
        int imported = 0;

        while ((pos = content.find("{", pos)) != std::string::npos)
        {
            size_t end = content.find("}", pos);
            if (end == std::string::npos)
                break;

            std::string obj = content.substr(pos, end - pos + 1);

            SavedCredential cred;
            cred.origin = ParseJsonString(obj, "origin");
            if (cred.origin.empty())
            {
                cred.origin = ParseJsonString(obj, "url");
            }
            cred.origin = ExtractOriginFromURL(cred.origin);
            cred.signon_realm = cred.origin;
            cred.username = ParseJsonString(obj, "username");
            cred.password = ParseJsonString(obj, "password");
            cred.notes = ParseJsonString(obj, "notes");

            if (cred.IsValid())
            {
                SaveCredential(cred);
                imported++;
            }

            pos = end + 1;
        }

        return imported > 0;
    }

    bool PasswordManager::SetMasterPassword(const std::string &password)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        master_password_hash_ = HashMasterPassword(password);
        settings_.require_master_password = true;

        // Re-encrypt all passwords with new key
        std::string new_key = DeriveKey(password);
        std::string old_key = encryption_key_;
        encryption_key_ = new_key;

        for (auto &cred : credentials_)
        {
            if (!cred.encrypted_password.empty())
            {
                // Decrypt with old key
                std::string temp = encryption_key_;
                encryption_key_ = old_key;
                cred.password = Decrypt(cred.encrypted_password);
                encryption_key_ = temp;

                // Re-encrypt with new key
                cred.encrypted_password = Encrypt(cred.password);
            }
        }

        SaveSettings();
        return Save();
    }

    bool PasswordManager::VerifyMasterPassword(const std::string &password) const
    {
        if (master_password_hash_.empty())
            return true;
        return HashMasterPassword(password) == master_password_hash_;
    }

    bool PasswordManager::HasMasterPassword() const
    {
        return !master_password_hash_.empty();
    }

    bool PasswordManager::RemoveMasterPassword(const std::string &current_password)
    {
        if (!VerifyMasterPassword(current_password))
            return false;

        std::lock_guard<std::mutex> lock(mutex_);

        master_password_hash_.clear();
        settings_.require_master_password = false;

        // Re-encrypt with default key
        std::string new_key = GetEncryptionKey();
        std::string old_key = encryption_key_;
        encryption_key_ = new_key;

        for (auto &cred : credentials_)
        {
            if (!cred.encrypted_password.empty())
            {
                std::string temp = encryption_key_;
                encryption_key_ = old_key;
                cred.password = Decrypt(cred.encrypted_password);
                encryption_key_ = temp;
                cred.encrypted_password = Encrypt(cred.password);
            }
        }

        SaveSettings();
        return Save();
    }

    bool PasswordManager::ChangeMasterPassword(const std::string &old_password, const std::string &new_password)
    {
        if (!VerifyMasterPassword(old_password))
            return false;
        return SetMasterPassword(new_password);
    }

    bool PasswordManager::IsLocked() const
    {
        return is_locked_;
    }

    bool PasswordManager::Unlock(const std::string &master_password)
    {
        if (!VerifyMasterPassword(master_password))
            return false;

        std::lock_guard<std::mutex> lock(mutex_);
        is_locked_ = false;
        last_activity_ = std::chrono::steady_clock::now();

        if (!master_password.empty())
        {
            encryption_key_ = DeriveKey(master_password);
        }

        return true;
    }

    void PasswordManager::Lock()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        is_locked_ = true;

        // Clear decrypted passwords from memory
        for (auto &cred : credentials_)
        {
            std::fill(cred.password.begin(), cred.password.end(), '\0');
            cred.password.clear();
        }
    }

    PasswordManager::Settings &PasswordManager::GetSettings()
    {
        return settings_;
    }

    const PasswordManager::Settings &PasswordManager::GetSettings() const
    {
        return settings_;
    }

    void PasswordManager::SaveSettings()
    {
        std::ofstream file(settings_file_);
        if (!file.is_open())
            return;

        file << "{\n"
             << "  \"offer_to_save_passwords\": " << (settings_.offer_to_save_passwords ? "true" : "false") << ",\n"
             << "  \"auto_signin\": " << (settings_.auto_signin ? "true" : "false") << ",\n"
             << "  \"check_passwords_leaked\": " << (settings_.check_passwords_leaked ? "true" : "false") << ",\n"
             << "  \"generate_passwords_automatically\": " << (settings_.generate_passwords_automatically ? "true" : "false") << ",\n"
             << "  \"auto_lock_timeout_minutes\": " << settings_.auto_lock_timeout_minutes << ",\n"
             << "  \"require_master_password\": " << (settings_.require_master_password ? "true" : "false") << ",\n"
             << "  \"master_password_hash\": \"" << EscapeJsonStr(master_password_hash_) << "\"\n"
             << "}\n";
    }

    PasswordManager::Stats PasswordManager::GetStats() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        Stats stats = {};
        stats.total_passwords = credentials_.size();
        stats.blacklisted_sites = blacklisted_origins_.size();

        std::map<std::string, int> password_counts;
        uint64_t ninety_days_ago = GetCurrentTimestamp() - (90ULL * 24 * 60 * 60 * 1000);

        for (const auto &cred : credentials_)
        {
            if (cred.blacklisted)
                continue;

            // Check for weak passwords
            std::string password = cred.password;
            if (password.empty() && !cred.encrypted_password.empty())
            {
                password = Decrypt(cred.encrypted_password);
            }

            auto strength = CheckPasswordStrength(password);
            if (strength.strength <= PasswordStrength::Weak)
            {
                stats.weak_passwords++;
            }

            // Check for reused passwords
            if (!password.empty())
            {
                password_counts[password]++;
            }

            // Check for old passwords
            if (cred.date_password_modified < ninety_days_ago)
            {
                stats.old_passwords++;
            }
        }

        // Count reused passwords
        for (const auto &[pass, count] : password_counts)
        {
            if (count > 1)
            {
                stats.reused_passwords += count;
            }
        }

        return stats;
    }

    bool PasswordManager::Load()
    {
        if (!std::filesystem::exists(passwords_file_))
        {
            return false;
        }

        std::ifstream file(passwords_file_, std::ios::binary);
        if (!file.is_open())
            return false;

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        // Decrypt the file content
        if (!content.empty())
        {
            content = Decrypt(content);
        }

        credentials_.clear();

        // Parse JSON array
        size_t pos = 0;
        while ((pos = content.find("{", pos)) != std::string::npos)
        {
            size_t end = content.find("}", pos);
            if (end == std::string::npos)
                break;

            std::string obj = content.substr(pos, end - pos + 1);

            SavedCredential cred;
            cred.id = ParseJsonString(obj, "id");
            cred.origin = ParseJsonString(obj, "origin");
            cred.signon_realm = ParseJsonString(obj, "signon_realm");
            cred.username = ParseJsonString(obj, "username");
            cred.encrypted_password = ParseJsonString(obj, "encrypted_password");
            cred.username_field = ParseJsonString(obj, "username_field");
            cred.password_field = ParseJsonString(obj, "password_field");
            cred.form_action = ParseJsonString(obj, "form_action");
            cred.date_created = ParseJsonUint64(obj, "date_created");
            cred.date_last_used = ParseJsonUint64(obj, "date_last_used");
            cred.date_password_modified = ParseJsonUint64(obj, "date_password_modified");
            cred.times_used = static_cast<uint32_t>(ParseJsonUint64(obj, "times_used"));
            cred.blacklisted = ParseJsonBool(obj, "blacklisted");
            cred.notes = ParseJsonString(obj, "notes");

            if (!cred.id.empty() && !cred.origin.empty())
            {
                credentials_.push_back(cred);
            }

            pos = end + 1;
        }

        return true;
    }

    bool PasswordManager::Save()
    {
        std::ostringstream json;
        json << "[\n";

        bool first = true;
        for (const auto &cred : credentials_)
        {
            if (!first)
                json << ",\n";
            first = false;

            json << "  {\n"
                 << "    \"id\": \"" << EscapeJsonStr(cred.id) << "\",\n"
                 << "    \"origin\": \"" << EscapeJsonStr(cred.origin) << "\",\n"
                 << "    \"signon_realm\": \"" << EscapeJsonStr(cred.signon_realm) << "\",\n"
                 << "    \"username\": \"" << EscapeJsonStr(cred.username) << "\",\n"
                 << "    \"encrypted_password\": \"" << EscapeJsonStr(cred.encrypted_password) << "\",\n"
                 << "    \"username_field\": \"" << EscapeJsonStr(cred.username_field) << "\",\n"
                 << "    \"password_field\": \"" << EscapeJsonStr(cred.password_field) << "\",\n"
                 << "    \"form_action\": \"" << EscapeJsonStr(cred.form_action) << "\",\n"
                 << "    \"date_created\": " << cred.date_created << ",\n"
                 << "    \"date_last_used\": " << cred.date_last_used << ",\n"
                 << "    \"date_password_modified\": " << cred.date_password_modified << ",\n"
                 << "    \"times_used\": " << cred.times_used << ",\n"
                 << "    \"blacklisted\": " << (cred.blacklisted ? "true" : "false") << ",\n"
                 << "    \"notes\": \"" << EscapeJsonStr(cred.notes) << "\"\n"
                 << "  }";
        }

        json << "\n]\n";

        // Encrypt and write
        std::string encrypted = Encrypt(json.str());

        std::ofstream file(passwords_file_, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return false;

        file.write(encrypted.data(), encrypted.size());
        return file.good();
    }

    void PasswordManager::SetSavePromptCallback(SavePromptCallback callback)
    {
        save_prompt_callback_ = std::move(callback);
    }

    void PasswordManager::SetUpdatePromptCallback(UpdatePromptCallback callback)
    {
        update_prompt_callback_ = std::move(callback);
    }

    void PasswordManager::SetCredentialSelectedCallback(CredentialSelectedCallback callback)
    {
        credential_selected_callback_ = std::move(callback);
    }

    std::string PasswordManager::ExtractOriginFromURL(const std::string &url)
    {
        if (url.empty())
            return "";

        // Find protocol
        size_t proto_end = url.find("://");
        if (proto_end == std::string::npos)
        {
            // No protocol, assume https
            return "https://" + url.substr(0, url.find('/'));
        }

        std::string protocol = url.substr(0, proto_end);
        size_t host_start = proto_end + 3;

        // Find end of host (port or path)
        size_t host_end = url.find_first_of(":/", host_start);
        if (host_end == std::string::npos)
        {
            host_end = url.length();
        }

        std::string host = url.substr(host_start, host_end - host_start);

        // Include port if non-standard
        std::string port;
        if (host_end < url.length() && url[host_end] == ':')
        {
            size_t port_end = url.find('/', host_end);
            if (port_end == std::string::npos)
                port_end = url.length();
            port = url.substr(host_end, port_end - host_end);

            // Skip default ports
            if ((protocol == "http" && port == ":80") ||
                (protocol == "https" && port == ":443"))
            {
                port.clear();
            }
        }

        return protocol + "://" + host + port;
    }

    std::string PasswordManager::GenerateUUID()
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);

        const char *hex = "0123456789abcdef";
        std::string uuid = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";

        for (char &c : uuid)
        {
            if (c == 'x')
            {
                c = hex[dis(gen)];
            }
            else if (c == 'y')
            {
                c = hex[(dis(gen) & 0x3) | 0x8];
            }
        }

        return uuid;
    }

    uint64_t PasswordManager::GetCurrentTimestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }

    std::string PasswordManager::Encrypt(const std::string &plaintext) const
    {
        if (plaintext.empty())
            return "";

#ifdef _WIN32
        // Use Windows DPAPI
        DATA_BLOB input;
        input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plaintext.data()));
        input.cbData = static_cast<DWORD>(plaintext.size());

        DATA_BLOB output;
        if (CryptProtectData(&input, nullptr, nullptr, nullptr, nullptr,
                             CRYPTPROTECT_UI_FORBIDDEN, &output))
        {
            std::string result(reinterpret_cast<char *>(output.pbData), output.cbData);
            LocalFree(output.pbData);
            return Base64Encode(result);
        }
        return Base64Encode(plaintext); // Fallback: just encode
#elif defined(__APPLE__)
        // Simple XOR encryption with key for macOS (Keychain would be better for production)
        std::string result = plaintext;
        for (size_t i = 0; i < result.size(); i++)
        {
            result[i] ^= encryption_key_[i % encryption_key_.size()];
        }
        return Base64Encode(result);
#else
        // Simple XOR encryption for Linux
        std::string result = plaintext;
        for (size_t i = 0; i < result.size(); i++)
        {
            result[i] ^= encryption_key_[i % encryption_key_.size()];
        }
        return Base64Encode(result);
#endif
    }

    std::string PasswordManager::Decrypt(const std::string &ciphertext) const
    {
        if (ciphertext.empty())
            return "";

#ifdef _WIN32
        std::string decoded = Base64Decode(ciphertext);

        DATA_BLOB input;
        input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(decoded.data()));
        input.cbData = static_cast<DWORD>(decoded.size());

        DATA_BLOB output;
        if (CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                               CRYPTPROTECT_UI_FORBIDDEN, &output))
        {
            std::string result(reinterpret_cast<char *>(output.pbData), output.cbData);
            LocalFree(output.pbData);
            return result;
        }
        return decoded; // Fallback
#elif defined(__APPLE__)
        std::string decoded = Base64Decode(ciphertext);
        for (size_t i = 0; i < decoded.size(); i++)
        {
            decoded[i] ^= encryption_key_[i % encryption_key_.size()];
        }
        return decoded;
#else
        std::string decoded = Base64Decode(ciphertext);
        for (size_t i = 0; i < decoded.size(); i++)
        {
            decoded[i] ^= encryption_key_[i % encryption_key_.size()];
        }
        return decoded;
#endif
    }

    std::string PasswordManager::HashMasterPassword(const std::string &password) const
    {
        // Simple SHA-256 like hash (production should use bcrypt/argon2)
        std::string salted = "UltralightBrowser_" + password + "_Salt2024";

        // Simple hash function
        uint64_t hash = 14695981039346656037ULL; // FNV offset basis
        for (char c : salted)
        {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ULL; // FNV prime
        }

        std::ostringstream ss;
        ss << std::hex << std::setfill('0') << std::setw(16) << hash;
        return ss.str();
    }

    std::string PasswordManager::DeriveKey(const std::string &password) const
    {
        // Simple key derivation (production should use PBKDF2/scrypt)
        std::string key = "UltralightPWKey_" + password;

        // Stretch to 32 bytes
        while (key.size() < 32)
        {
            key += key;
        }

        return key.substr(0, 32);
    }

    void PasswordManager::LoadBlacklist()
    {
        blacklisted_origins_.clear();

        if (!std::filesystem::exists(blacklist_file_))
            return;

        std::ifstream file(blacklist_file_);
        if (!file.is_open())
            return;

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        // Simple JSON array parsing
        size_t pos = 0;
        while ((pos = content.find('"', pos)) != std::string::npos)
        {
            pos++;
            size_t end = content.find('"', pos);
            if (end == std::string::npos)
                break;

            std::string origin = content.substr(pos, end - pos);
            if (!origin.empty() && origin.find("://") != std::string::npos)
            {
                blacklisted_origins_.push_back(origin);
            }

            pos = end + 1;
        }
    }

    void PasswordManager::SaveBlacklist()
    {
        std::ofstream file(blacklist_file_);
        if (!file.is_open())
            return;

        file << "[\n";
        for (size_t i = 0; i < blacklisted_origins_.size(); i++)
        {
            file << "  \"" << EscapeJsonStr(blacklisted_origins_[i]) << "\"";
            if (i < blacklisted_origins_.size() - 1)
                file << ",";
            file << "\n";
        }
        file << "]\n";
    }

    void PasswordManager::LoadSettings()
    {
        settings_ = Settings(); // Defaults

        if (!std::filesystem::exists(settings_file_))
            return;

        std::ifstream file(settings_file_);
        if (!file.is_open())
            return;

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        settings_.offer_to_save_passwords = ParseJsonBool(content, "offer_to_save_passwords", true);
        settings_.auto_signin = ParseJsonBool(content, "auto_signin", true);
        settings_.check_passwords_leaked = ParseJsonBool(content, "check_passwords_leaked", false);
        settings_.generate_passwords_automatically = ParseJsonBool(content, "generate_passwords_automatically", true);
        settings_.auto_lock_timeout_minutes = static_cast<int>(ParseJsonUint64(content, "auto_lock_timeout_minutes"));
        settings_.require_master_password = ParseJsonBool(content, "require_master_password", false);
        master_password_hash_ = ParseJsonString(content, "master_password_hash");
    }

    std::string PasswordManager::GetEncryptionKey() const
    {
        // Generate a machine-specific key
        std::string key;

#ifdef _WIN32
        // Use machine GUID on Windows
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography",
                          0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS)
        {
            char buffer[256];
            DWORD bufferSize = sizeof(buffer);
            if (RegQueryValueExA(hKey, "MachineGuid", nullptr, nullptr,
                                 reinterpret_cast<LPBYTE>(buffer), &bufferSize) == ERROR_SUCCESS)
            {
                key = std::string(buffer, bufferSize - 1);
            }
            RegCloseKey(hKey);
        }
#elif defined(__APPLE__)
        // Use a fixed identifier for macOS (could use hardware UUID)
        key = "UltralightBrowser_MacOS_Key_2024";
#else
        // Use machine-id on Linux
        std::ifstream machine_id("/etc/machine-id");
        if (machine_id.is_open())
        {
            std::getline(machine_id, key);
        }
#endif

        if (key.empty())
        {
            key = "UltralightBrowser_DefaultKey_2024";
        }

        // Ensure key is 32 bytes
        while (key.size() < 32)
        {
            key += key;
        }

        return key.substr(0, 32);
    }

    void PasswordManager::RegenerateEncryptionKey()
    {
        encryption_key_ = GetEncryptionKey();
    }

    // Global instance
    static std::unique_ptr<PasswordManager> g_password_manager;

    PasswordManager &GetPasswordManager()
    {
        if (!g_password_manager)
        {
            g_password_manager = std::make_unique<PasswordManager>();
        }
        return *g_password_manager;
    }

} // namespace password
