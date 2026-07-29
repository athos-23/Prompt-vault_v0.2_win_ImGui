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

// Modalita di Visualizzazione (0 = TXT Editor, 1 = Markdown Preview)
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

void DeleteProjectFolder(const std::string& projName) {
    std::vector<size_t> toDelete;
    for (size_t i = 0; i < g_prompts.size(); ++i) {
        std::string pProj = g_prompts[i].project.empty() ? "Senza Progetto" : g_prompts[i].project;
        if (pProj == projName) {
            std::string filename = SanitizeFilename(g_prompts[i].title) + "_" + g_prompts[i].id + ".txt";
            fs::remove(DATA_DIR + "/" + filename);
            toDelete.push_back(i);
        }
    }
    
    for (int i = (int)toDelete.size() - 1; i >= 0; --i) {
        g_prompts.erase(g_prompts.begin() + toDelete[i]);
    }

    g_selectedPrompt = -1;
    g_bufTitle[0] = '\0';
    g_bufProject[0] = '\0';
    g_bufTags[0] = '\0';
    g_bufContent[0] = '\0';
    
    if (g_activeFilterType == "project" && g_activeFilterValue == projName) {
        g_activeFilterType = "";
        g_activeFilterValue = "";
    }

    ShowToast("Progetto eliminato!");
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
    ImGui::BeginChild("MarkdownPreviewArea", ImVec2(0, -45), true, ImGuiWindowFlags_HorizontalScrollbar);
    
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
                ImGui::SetWindowFontScale(1.3f);
                ImGui::TextColored(ImVec4(0.39f, 0.40f, 0.95f, 1.00f), "%s", line.substr(2).c_str());
                ImGui::SetWindowFontScale(1.0f);
            } else if (line.rfind("## ", 0) == 0) {
                ImGui::SetWindowFontScale(1.1f);
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
        if (ImGui::MenuItem("Incolla")) {
            const char* clipText = ImGui::GetClipboardText();
            if (clipText) {
                size_t len = strlen(clipText);
                if (len < bufSize - 1) {
                    strcpy(buf, clipText);
                }
            }
        }
        if (ImGui::MenuItem("Copia Tutto")) {
            ImGui::SetClipboardText(buf);
            ShowToast("Copiato!");
        }
        if (ImGui::MenuItem("Taglia Tutto")) {
            ImGui::SetClipboardText(buf);
            buf[0] = '\0';
            ShowToast("Tagliato!");
        }
        if (ImGui::MenuItem("Cancella Tutto")) {
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
    style.FramePadding = ImVec2(6, 4);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                  = ImVec4(0.92f, 0.92f, 0.94f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.12f, 0.12f, 0.13f, 1.00f);
    colors[ImGuiCol_Border]                = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.39f, 0.40f, 0.95f, 1.00f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.48f, 0.49f, 0.98f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance, nullptr, nullptr, nullptr, nullptr, L"PromptVaultClass", nullptr };
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    ::RegisterClassExW(&wc);

    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Prompt Vault", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &useDarkMode, sizeof(useDarkMode));

    HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    SetupObsidianTheme();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    LoadPromptsFromDisk();

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Prompt Vault Main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        // -------------------------------------------------------------
        // SIDEBAR PROGETTI & TAG
        // -------------------------------------------------------------
        ImGui::BeginChild("Sidebar", ImVec2(230, 0), true);
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 1.0f, 1.0f), "PROMPT VAULT");
        ImGui::Separator();

        if (ImGui::Button("+ Nuovo Prompt", ImVec2(-1, 30))) {
            g_selectedPrompt = -1;
            strcpy(g_bufTitle, "Nuovo Prompt");
            strcpy(g_bufProject, "Senza Progetto");
            strcpy(g_bufTags, "");
            strcpy(g_bufContent, "");
        }

        ImGui::Spacing();
        ImGui::TextDisabled("PROGETTI / CARTELLE");

        std::vector<std::string> projects;
        for (const auto& p : g_prompts) {
            std::string proj = p.project.empty() ? "Senza Progetto" : p.project;
            if (std::find(projects.begin(), projects.end(), proj) == projects.end()) {
                projects.push_back(proj);
            }
        }

        for (size_t i = 0; i < projects.size(); ++i) {
            const auto& proj = projects[i];
            int count = 0;
            for (const auto& p : g_prompts) {
                std::string pProj = p.project.empty() ? "Senza Progetto" : p.project;
                if (pProj == proj) count++;
            }

            std::string label = proj + " (" + std::to_string(count) + ")";
            
            if (ImGui::Selectable(label.c_str(), g_activeFilterType == "project" && g_activeFilterValue == proj, 0, ImVec2(150, 0))) {
                g_activeFilterType = "project";
                g_activeFilterValue = proj;
            }

            if (proj != "Senza Progetto") {
                ImGui::SameLine(185);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                std::string delBtnId = "X##DelProj_" + std::to_string(i);
                if (ImGui::Button(delBtnId.c_str(), ImVec2(24, 18))) {
                    DeleteProjectFolder(proj);
                }
                ImGui::PopStyleColor(2);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("TAG");

        std::vector<std::string> allTags;
        for (const auto& p : g_prompts) {
            std::stringstream ss(p.tags);
            std::string tag;
            while (std::getline(ss, tag, ',')) {
                tag.erase(0, tag.find_first_not_of(" "));
                tag.erase(tag.find_last_not_of(" ") + 1);
                if (!tag.empty() && std::find(allTags.begin(), allTags.end(), tag) == allTags.end()) {
                    allTags.push_back(tag);
                }
            }
        }

        for (const auto& tag : allTags) {
            std::string label = "# " + tag;
            if (ImGui::Selectable(label.c_str(), g_activeFilterType == "tag" && g_activeFilterValue == tag)) {
                g_activeFilterType = "tag";
                g_activeFilterValue = tag;
            }
        }

        float availHeight = ImGui::GetContentRegionAvail().y;
        if (availHeight > 100) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + availHeight - 90);
        }
        ImGui::Separator();
        if (ImGui::Button("Esporta DB (.json)", ImVec2(-1, 22))) { ExportDatabaseJSON(hwnd); }
        if (ImGui::Button("Importa DB (.json)", ImVec2(-1, 22))) { ImportDatabaseJSON(hwnd); }
        if (ImGui::Button("Esporta Tutti .MD", ImVec2(-1, 22))) { ExportAllAsMarkdown(); }

        ImGui::EndChild();

        ImGui::SameLine();

        // -------------------------------------------------------------
        // LISTA PROMPT COMPATTA
        // -------------------------------------------------------------
        ImGui::BeginChild("ListPanel", ImVec2(260, 0), true);

        ImGui::InputText("##Search", g_searchBuf, IM_ARRAYSIZE(g_searchBuf));

        if (!g_activeFilterType.empty()) {
            std::string filterLabel = "Filtro: " + g_activeFilterValue + " [X]";
            if (ImGui::Button(filterLabel.c_str(), ImVec2(-1, 20))) {
                g_activeFilterType = "";
                g_activeFilterValue = "";
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled("Prompt Salvati (%d)", (int)g_prompts.size());

        for (int i = 0; i < (int)g_prompts.size(); i++) {
            std::string searchStr = g_searchBuf;
            std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);
            std::string titleStr = g_prompts[i].title;
            std::transform(titleStr.begin(), titleStr.end(), titleStr.begin(), ::tolower);

            std::string projStr = g_prompts[i].project.empty() ? "Senza Progetto" : g_prompts[i].project;

            if (!searchStr.empty() && titleStr.find(searchStr) == std::string::npos) continue;
            if (g_activeFilterType == "project" && projStr != g_activeFilterValue) continue;
            if (g_activeFilterType == "tag" && g_prompts[i].tags.find(g_activeFilterValue) == std::string::npos) continue;

            ImGui::PushStyleColor(ImGuiCol_Header, GetCardColor(g_prompts[i].color));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, GetCardColor(g_prompts[i].color));
            
            std::string itemLabel = g_prompts[i].title.empty() ? "Senza Titolo" : g_prompts[i].title;
            itemLabel += "  [" + projStr + "]";
            
            if (ImGui::Selectable((itemLabel + "##" + std::to_string(i)).c_str(), g_selectedPrompt == i, 0, ImVec2(0, 0))) {
                g_selectedPrompt = i;
                strcpy(g_bufTitle, g_prompts[i].title.c_str());
                strcpy(g_bufProject, projStr.c_str());
                strcpy(g_bufTags, g_prompts[i].tags.c_str());
                strcpy(g_bufContent, g_prompts[i].content.c_str());
            }
            ImGui::PopStyleColor(2);
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // -------------------------------------------------------------
        // EDITOR & PANNELLO DI VISUALIZZAZIONE REATTIVO
        // -------------------------------------------------------------
        ImGui::BeginChild("EditorPanel", ImVec2(0, 0), true);
        if (g_selectedPrompt != -1 || strlen(g_bufTitle) > 0) {
            
            if (ImGui::RadioButton("[TXT] Editor", g_viewMode == 0)) g_viewMode = 0;
            ImGui::SameLine();
            if (ImGui::RadioButton("[MD] Anteprima", g_viewMode == 1)) g_viewMode = 1;

            ImGui::SameLine();
            ImGui::Text(" | Colore:"); ImGui::SameLine();
            
            std::string currentColor = (g_selectedPrompt != -1) ? g_prompts[g_selectedPrompt].color : "default";
            
            auto ApplyColorChange = [](const std::string& col) {
                if (g_selectedPrompt != -1) {
                    g_prompts[g_selectedPrompt].color = col;
                    SavePromptToDisk(g_prompts[g_selectedPrompt]);
                }
            };

            if (ImGui::RadioButton("Default", currentColor == "default")) { ApplyColorChange("default"); } ImGui::SameLine();
            if (ImGui::RadioButton("Blu", currentColor == "blue")) { ApplyColorChange("blue"); } ImGui::SameLine();
            if (ImGui::RadioButton("Verde", currentColor == "emerald")) { ApplyColorChange("emerald"); } ImGui::SameLine();
            if (ImGui::RadioButton("Viola", currentColor == "purple")) { ApplyColorChange("purple"); } ImGui::SameLine();
            if (ImGui::RadioButton("Rosa", currentColor == "rose")) { ApplyColorChange("rose"); } ImGui::SameLine();
            if (ImGui::RadioButton("Ambra", currentColor == "amber")) { ApplyColorChange("amber"); }

            ImGui::Separator();

            ImGui::Text("Titolo:");
            ImGui::SameLine(90);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##Titolo", g_bufTitle, IM_ARRAYSIZE(g_bufTitle));
            ShowContextMenuForBuffer(g_bufTitle, IM_ARRAYSIZE(g_bufTitle));

            ImGui::Text("Progetto:");
            ImGui::SameLine(90);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##Progetto", g_bufProject, IM_ARRAYSIZE(g_bufProject));
            ShowContextMenuForBuffer(g_bufProject, IM_ARRAYSIZE(g_bufProject));

            ImGui::Text("Tag:");
            ImGui::SameLine(90);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##Tag", g_bufTags, IM_ARRAYSIZE(g_bufTags));
            ShowContextMenuForBuffer(g_bufTags, IM_ARRAYSIZE(g_bufTags));

            ImGui::Separator();

            float contentHeight = ImGui::GetContentRegionAvail().y - 40;
            if (contentHeight < 100) contentHeight = 100;

            if (g_viewMode == 0) {
                // CORRETTO: flag valido per ImGui
                ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;
                ImGui::InputTextMultiline("##Content", g_bufContent, IM_ARRAYSIZE(g_bufContent), ImVec2(-1, contentHeight), flags);
                ShowContextMenuForBuffer(g_bufContent, IM_ARRAYSIZE(g_bufContent));
            } else {
                RenderMarkdownPreview(g_bufContent);
            }

            ImGui::Spacing();

            if (ImGui::Button("Salva", ImVec2(80, 28))) {
                Prompt p;
                if (g_selectedPrompt == -1) {
                    p.id = std::to_string(GetTickCount64());
                    p.color = "default";
                } else {
                    p.id = g_prompts[g_selectedPrompt].id;
                    p.color = g_prompts[g_selectedPrompt].color;
                }
                p.title = g_bufTitle;
                p.project = strlen(g_bufProject) == 0 ? "Senza Progetto" : g_bufProject;
                p.tags = g_bufTags;
                p.content = g_bufContent;

                SavePromptToDisk(p);
                LoadPromptsFromDisk();
                ShowToast("Salvato!");
            }

            ImGui::SameLine();
            if (ImGui::Button("Copia", ImVec2(80, 28))) {
                ImGui::SetClipboardText(g_bufContent);
                ShowToast("Copiato!");
            }

            ImGui::SameLine();
            if (ImGui::Button("Esporta MD", ImVec2(100, 28))) {
                Prompt p;
                p.title = g_bufTitle;
                p.project = strlen(g_bufProject) == 0 ? "Senza Progetto" : g_bufProject;
                p.tags = g_bufTags;
                p.content = g_bufContent;
                p.color = (g_selectedPrompt != -1) ? g_prompts[g_selectedPrompt].color : "default";
                ExportSinglePromptMD(p);
            }

            if (g_selectedPrompt != -1) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                if (ImGui::Button("Elimina", ImVec2(80, 28))) {
                    std::string filename = SanitizeFilename(g_prompts[g_selectedPrompt].title) + "_" + g_prompts[g_selectedPrompt].id + ".txt";
                    fs::remove(DATA_DIR + "/" + filename);
                    g_prompts.erase(g_prompts.begin() + g_selectedPrompt);
                    g_selectedPrompt = -1;
                    strcpy(g_bufTitle, "");
                    strcpy(g_bufProject, "");
                    strcpy(g_bufTags, "");
                    strcpy(g_bufContent, "");
                    ShowToast("Eliminato.");
                }
                ImGui::PopStyleColor(2);
            }
        } else {
            ImGui::TextDisabled("Seleziona un prompt a sinistra oppure creane uno nuovo.");
        }
        ImGui::EndChild();

        if (g_toastTimer > 0.0f) {
            g_toastTimer -= ImGui::GetIO().DeltaTime;
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 220, ImGui::GetIO().DisplaySize.y - 45));
            ImGui::SetNextWindowSize(ImVec2(200, 32));
            ImGui::Begin("Toast", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", g_toastMessage.c_str());
            ImGui::End();
        }

        ImGui::End();

        ImGui::Render();
        const float clear_color[4] = { 0.09f, 0.09f, 0.10f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
