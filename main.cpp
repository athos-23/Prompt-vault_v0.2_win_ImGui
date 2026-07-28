#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <commdlg.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "resource.h"

#pragma comment(lib, "dwmapi.lib")

namespace fs = std::filesystem;

// Struttura Dati Prompt
struct Prompt {
    std::string id;
    std::string title;
    std::string project;
    std::string tags;
    std::string color = "default";
    std::string content;
};

// Variabili Globali DirectX e Windows
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

std::vector<Prompt> g_prompts;
int g_selectedPrompt = -1;
char g_searchBuf[256] = "";

// Buffer Editor
char g_bufTitle[512] = "";
char g_bufProject[512] = "";
char g_bufTags[512] = "";
char g_bufContent[65536] = ""; // Buffer fino a 64KB

// Modalità di Visualizzazione (0 = TXT Editor, 1 = Markdown Preview)
int g_viewMode = 0;

// Gestione Filtri
std::string g_activeFilterType = ""; // "project" o "tag"
std::string g_activeFilterValue = "";

// Toast
std::string g_toastMessage = "";
float g_toastTimer = 0.0f;

const std::string DATA_DIR = "PromptVault_Data";

// Prototipi
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void ShowToast(const std::string& msg) {
    g_toastMessage = msg;
    g_toastTimer = 3.0f;
}

void EnsureDataFolder() {
    if (!fs::exists(DATA_DIR)) fs::create_directory(DATA_DIR);
}

std::string SanitizeFilename(const std::string& name) {
    std::string clean = name;
    std::replace_if(clean.begin(), clean.end(), [](char c) {
        return c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
    }, '_');
    if (clean.empty()) clean = "prompt";
    return clean;
}

void SavePromptToDisk(const Prompt& p) {
    EnsureDataFolder();
    std::string filename = SanitizeFilename(p.title) + "_" + p.id + ".txt";
    std::ofstream file(DATA_DIR + "/" + filename, std::ios::binary);
    if (file.is_open()) {
        file << p.id << "\n"
             << p.title << "\n" 
             << (p.project.empty() ? "Senza Progetto" : p.project) << "\n" 
             << p.tags << "\n" 
             << p.color << "\n" 
             << p.content;
    }
}

void LoadPromptsFromDisk() {
    g_prompts.clear();
    EnsureDataFolder();
    for (const auto& entry : fs::directory_iterator(DATA_DIR)) {
        if (entry.path().extension() == ".txt") {
            std::ifstream file(entry.path(), std::ios::binary);
            if (file.is_open()) {
                Prompt p;
                std::getline(file, p.id);
                std::getline(file, p.title);
                std::getline(file, p.project);
                std::getline(file, p.tags);
                std::getline(file, p.color);
                if (p.color.empty()) p.color = "default";
                if (p.project.empty()) p.project = "Senza Progetto";

                std::string line, content;
                while (std::getline(file, line)) content += line + "\n";
                p.content = content;
                g_prompts.push_back(p);
            }
        }
    }
}

std::string EscapeJSON(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default: o << c;
        }
    }
    return o.str();
}

void ExportDatabaseJSON(HWND hwnd) {
    OPENFILENAMEA ofn;
    char szFile[260] = "prompt_vault_backup.json";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameA(&ofn) == TRUE) {
        std::ofstream file(ofn.lpstrFile, std::ios::binary);
        if (file.is_open()) {
            file << "[\n";
            for (size_t i = 0; i < g_prompts.size(); ++i) {
                const auto& p = g_prompts[i];
                file << "  {\n";
                file << "    \"id\": \"" << EscapeJSON(p.id) << "\",\n";
                file << "    \"title\": \"" << EscapeJSON(p.title) << "\",\n";
                file << "    \"project\": \"" << EscapeJSON(p.project) << "\",\n";
                file << "    \"tags\": \"" << EscapeJSON(p.tags) << "\",\n";
                file << "    \"color\": \"" << EscapeJSON(p.color) << "\",\n";
                file << "    \"content\": \"" << EscapeJSON(p.content) << "\"\n";
                file << "  }" << (i + 1 < g_prompts.size() ? "," : "") << "\n";
            }
            file << "]\n";
            ShowToast("Database JSON Esportato!");
        }
    }
}

std::string ExtractJSONValue(const std::string& line, const std::string& key) {
    size_t pos = line.find("\"" + key + "\":");
    if (pos != std::string::npos) {
        size_t start = line.find("\"", pos + key.length() + 3);
        size_t end = line.rfind("\"");
        if (start != std::string::npos && end > start) {
            return line.substr(start + 1, end - start - 1);
        }
    }
    return "";
}

void ImportDatabaseJSON(HWND hwnd) {
    OPENFILENAMEA ofn;
    char szFile[260] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        std::ifstream file(ofn.lpstrFile, std::ios::binary);
        if (file.is_open()) {
            std::string line;
            Prompt currentPrompt;
            bool insidePrompt = false;

            while (std::getline(file, line)) {
                if (line.find("{") != std::string::npos) {
                    insidePrompt = true;
                    currentPrompt = Prompt();
                } else if (line.find("}") != std::string::npos && insidePrompt) {
                    if (currentPrompt.id.empty()) currentPrompt.id = std::to_string(GetTickCount64());
                    if (currentPrompt.project.empty()) currentPrompt.project = "Senza Progetto";
                    SavePromptToDisk(currentPrompt);
                    insidePrompt = false;
                } else if (insidePrompt) {
                    if (line.find("\"id\":") != std::string::npos) currentPrompt.id = ExtractJSONValue(line, "id");
                    if (line.find("\"title\":") != std::string::npos) currentPrompt.title = ExtractJSONValue(line, "title");
                    if (line.find("\"project\":") != std::string::npos) currentPrompt.project = ExtractJSONValue(line, "project");
                    if (line.find("\"tags\":") != std::string::npos) currentPrompt.tags = ExtractJSONValue(line, "tags");
                    if (line.find("\"color\":") != std::string::npos) currentPrompt.color = ExtractJSONValue(line, "color");
                    if (line.find("\"content\":") != std::string::npos) currentPrompt.content = ExtractJSONValue(line, "content");
                }
            }
            LoadPromptsFromDisk();
            ShowToast("Database Importato!");
        }
    }
}

void ExportSinglePromptMD(const Prompt& p) {
    std::string exportDir = "Markdown_Exports";
    if (!fs::exists(exportDir)) fs::create_directory(exportDir);

    std::string filename = SanitizeFilename(p.title);
    std::ofstream file(exportDir + "/" + filename + ".md", std::ios::binary);
    if (file.is_open()) {
        file << "---\n";
        file << "titolo: \"" << p.title << "\"\n";
        file << "progetto: \"" << (p.project.empty() ? "Senza Progetto" : p.project) << "\"\n";
        file << "tag: [" << p.tags << "]\n";
        file << "colore: \"" << p.color << "\"\n";
        file << "---\n\n";
        file << p.content;
        ShowToast("File MD Esportato!");
    }
}

void ExportAllAsMarkdown() {
    for (const auto& p : g_prompts) {
        ExportSinglePromptMD(p);
    }
    ShowToast("Tutti i file .md esportati!");
}

void RenderMarkdownPreview(const std::string& text) {
    ImGui::BeginChild("MarkdownPreviewArea", ImVec2(0, -50), true, ImGuiWindowFlags_HorizontalScrollbar);
    
    std::stringstream ss(text);
    std::string line;
    bool inCodeBlock = false;

    while (std::getline(ss, line)) {
        if (line.rfind("```", 0) == 0) {
            inCodeBlock = !inCodeBlock;
            ImGui::Separator();
            continue;
        }

        if (inCodeBlock) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
            ImGui::TextWrapped("%s", line.c_str());
            ImGui::PopStyleColor();
        } else {
            if (line.rfind("# ", 0) == 0) {
                ImGui::SetWindowFontScale(1.4f);
                ImGui::TextColored(ImVec4(0.39f, 0.40f, 0.95f, 1.00f), "%s", line.substr(2).c_str());
                ImGui::SetWindowFontScale(1.0f);
            } else if (line.rfind("## ", 0) == 0) {
                ImGui::SetWindowFontScale(1.2f);
                ImGui::TextColored(ImVec4(0.30f, 0.70f, 0.90f, 1.00f), "%s", line.substr(3).c_str());
                ImGui::SetWindowFontScale(1.0f);
            } else if (line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0) {
                ImGui::BulletText("%s", line.substr(2).c_str());
            } else {
                ImGui::TextWrapped("%s", line.c_str());
            }
        }
    }

    ImGui::EndChild();
}

void ShowContextMenuForBuffer(char* buf, size_t bufSize) {
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("📋 Incolla")) {
            const char* clipText = ImGui::GetClipboardText();
            if (clipText) {
                size_t len = strlen(clipText);
                if (len < bufSize - 1) {
                    strcpy(buf, clipText);
                }
            }
        }
        if (ImGui::MenuItem("📑 Copia Tutto")) {
            ImGui::SetClipboardText(buf);
            ShowToast("Copiato!");
        }
        if (ImGui::MenuItem("✂️ Taglia Tutto")) {
            ImGui::SetClipboardText(buf);
            buf[0] = '\0';
            ShowToast("Tagliato!");
        }
        if (ImGui::MenuItem("🗑️ Cancella Tutto")) {
            buf[0] = '\0';
        }
        ImGui::EndPopup();
    }
}

ImVec4 GetCardColor(const std::string& colorName) {
    if (colorName == "blue")    return ImVec4(0.12f, 0.25f, 0.45f, 0.85f);
    if (colorName == "emerald") return ImVec4(0.10f, 0.35f, 0.22f, 0.85f);
    if (colorName == "amber")   return ImVec4(0.40f, 0.30f, 0.10f, 0.85f);
    if (colorName == "purple")  return ImVec4(0.35f, 0.15f, 0.45f, 0.85f);
    if (colorName == "rose")    return ImVec4(0.45f, 0.15f, 0.25f, 0.85f);
    return ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
}

void SetupObsidianTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.FramePadding = ImVec2
