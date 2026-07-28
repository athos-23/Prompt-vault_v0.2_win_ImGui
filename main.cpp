#include <windows.h>
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
char g_searchBuf[128] = "";

// Buffer Editor
char g_bufTitle[256] = "";
char g_bufProject[256] = "";
char g_bufTags[256] = "";
char g_bufContent[16384] = "";

// Gestione Filtri
std::string g_activeFilterType = ""; // "project" o "tag"
std::string g_activeFilterValue = "";

// Notifiche Toast
std::string g_toastMessage = "";
float g_toastTimer = 0.0f;

const std::string DATA_DIR = "PromptVault_Data";

// Helper Prototipi
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

void SavePromptToDisk(const Prompt& p) {
    EnsureDataFolder();
    std::ofstream file(DATA_DIR + "/" + p.id + ".txt");
    if (file.is_open()) {
        file << p.title << "\n" 
             << p.project << "\n" 
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
            std::ifstream file(entry.path());
            if (file.is_open()) {
                Prompt p;
                p.id = entry.path().stem().string();
                std::getline(file, p.title);
                std::getline(file, p.project);
                std::getline(file, p.tags);
                std::getline(file, p.color);
                if (p.color.empty()) p.color = "default";

                std::string line, content;
                while (std::getline(file, line)) content += line + "\n";
                p.content = content;
                g_prompts.push_back(p);
            }
        }
    }
}

// Funzione Helper di Escape per JSON manuale
std::string EscapeJSON(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if ('\x00' <= c && c <= '\x1f') {
                    o << "\\u" << std::hex << (int)c;
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

// Esporta Database JSON
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
        std::ofstream file(ofn.lpstrFile);
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
            ShowToast("Database JSON esportato!");
        }
    }
}

// Importa Database JSON
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
        std::ifstream file(ofn.lpstrFile);
        if (file.is_open()) {
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            // Parsing semplice riga per riga / chiavi
            ShowToast("Importazione completata!");
            LoadPromptsFromDisk();
        }
    }
}

// Esporta Singolo Prompt in Markdown
void ExportSinglePromptMD(const Prompt& p) {
    std::string exportDir = "Markdown_Exports";
    if (!fs::exists(exportDir)) fs::create_directory(exportDir);

    std::string filename = p.title;
    std::replace_if(filename.begin(), filename.end(), [](char c){ return !isalnum(c); }, '_');
    if (filename.empty()) filename = "prompt";

    std::ofstream file(exportDir + "/" + filename + ".md");
    if (file.is_open()) {
        file << "---\n";
        file << "titolo: \"" << p.title << "\"\n";
        file << "progetto: \"" << p.project << "\"\n";
        file << "tag: [" << p.tags << "]\n";
        file << "colore: \"" << p.color << "\"\n";
        file << "---\n\n";
        file << p.content;
        ShowToast("Prompt salvato in 'Markdown_Exports'!");
    }
}

// Esporta Tutti i Prompt in Markdown
void ExportAllAsMarkdown() {
    std::string exportDir = "Markdown_Exports";
    if (!fs::exists(exportDir)) fs::create_directory(exportDir);

    for (const auto& p : g_prompts) {
        ExportSinglePromptMD(p);
    }
    ShowToast("Tutti i file .md esportati!");
}

// Colori Schede
ImVec4 GetCardColor(const std::string& colorName) {
    if (colorName == "blue")    return ImVec4(0.10f, 0.20f, 0.40f, 0.60f);
    if (colorName == "emerald") return ImVec4(0.08f, 0.30f, 0.20f, 0.60f);
    if (colorName == "amber")   return ImVec4(0.35f, 0.25f, 0.08f, 0.60f);
    if (colorName == "purple")  return ImVec4(0.30f, 0.12f, 0.40f, 0.60f);
    if (colorName == "rose")    return ImVec4(0.40f, 0.12f, 0.20f, 0.60f);
    return ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
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
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance, nullptr, nullptr, nullptr, nullptr, L"PromptVaultClass", nullptr };
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    ::RegisterClassExW(&wc);

    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Prompt Vault", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

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
        // COLONNA 1: SIDEBAR PROGETTI, TAG E IMPORT/EXPORT DATABASE
        // -------------------------------------------------------------
        ImGui::BeginChild("Sidebar", ImVec2(220, 0), true);
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 1.0f, 1.0f), "PROMPT VAULT");
        ImGui::Separator();

        if (ImGui::Button("+ Nuovo Prompt", ImVec2(-1, 30))) {
            g_selectedPrompt = -1;
            strcpy(g_bufTitle, "Nuovo Prompt");
            strcpy(g_bufProject, "");
            strcpy(g_bufTags, "");
            strcpy(g_bufContent, "");
        }

        ImGui::Spacing();
        ImGui::TextDisabled("PROGETTI / CARTELLE");

        std::vector<std::string> projects;
        for (const auto& p : g_prompts) {
            if (!p.project.empty() && std::find(projects.begin(), projects.end(), p.project) == projects.end()) {
                projects.push_back(p.project);
            }
        }

        for (const auto& proj : projects) {
            int count = 0;
            for (const auto& p : g_prompts) if (p.project == proj) count++;

            std::string label = " > " + proj + " (" + std::to_string(count) + ")";
            if (ImGui::Selectable(label.c_str(), g_activeFilterType == "project" && g_activeFilterValue == proj)) {
                g_activeFilterType = "project";
                g_activeFilterValue = proj;
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

        // Tasti Importa/Esporta Database SO PRA al tasto Esporta File .MD
        float availHeight = ImGui::GetContentRegionAvail().y;
        if (availHeight > 120) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + availHeight - 110);
        }
        ImGui::Separator();
        if (ImGui::Button("Esporta DB (.json)", ImVec2(-1, 24))) {
            ExportDatabaseJSON(hwnd);
        }
        if (ImGui::Button("Importa DB (.json)", ImVec2(-1, 24))) {
            ImportDatabaseJSON(hwnd);
        }
        if (ImGui::Button("Esporta File .MD", ImVec2(-1, 24))) {
            ExportAllAsMarkdown();
        }

        ImGui::EndChild();

        ImGui::SameLine();

        // -------------------------------------------------------------
        // COLONNA 2: LISTA PROMPT COMPATTA (COMPACT RECTANGLE)
        // -------------------------------------------------------------
        ImGui::BeginChild("ListPanel", ImVec2(280, 0), true);

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

            if (!searchStr.empty() && titleStr.find(searchStr) == std::string::npos) continue;
            if (g_activeFilterType == "project" && g_prompts[i].project != g_activeFilterValue) continue;
            if (g_activeFilterType == "tag" && g_prompts[i].tags.find(g_activeFilterValue) == std::string::npos) continue;

            ImGui::PushStyleColor(ImGuiCol_Header, GetCardColor(g_prompts[i].color));
            
            // Riquadro compatto adattato alle dimensioni del testo
            std::string itemLabel = g_prompts[i].title.empty() ? "Senza Titolo" : g_prompts[i].title;
            if (!g_prompts[i].project.empty()) {
                itemLabel += "  (" + g_prompts[i].project + ")";
            }
            
            if (ImGui::Selectable((itemLabel + "##" + std::to_string(i)).c_str(), g_selectedPrompt == i, 0, ImVec2(0, 0))) {
                g_selectedPrompt = i;
                strcpy(g_bufTitle, g_prompts[i].title.c_str());
                strcpy(g_bufProject, g_prompts[i].project.c_str());
                strcpy(g_bufTags, g_prompts[i].tags.c_str());
                strcpy(g_bufContent, g_prompts[i].content.c_str());
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // -------------------------------------------------------------
        // COLONNA 3: EDITOR CON TASTO COPIA E ESPORTA MD SINGOLO
        // -------------------------------------------------------------
        ImGui::BeginChild("EditorPanel", ImVec2(0, 0), true);
        if (g_selectedPrompt != -1 || strlen(g_bufTitle) > 0) {
            
            ImGui::Text("Colore Scheda:"); ImGui::SameLine();
            if (ImGui::RadioButton("Default", g_selectedPrompt != -1 && g_prompts[g_selectedPrompt].color == "default")) { if (g_selectedPrompt != -1) g_prompts[g_selectedPrompt].color = "default"; } ImGui::SameLine();
            if (ImGui::RadioButton("Blu", g_selectedPrompt != -1 && g_prompts[g_selectedPrompt].color == "blue")) { if (g_selectedPrompt != -1) g_prompts[g_selectedPrompt].color = "blue"; } ImGui::SameLine();
            if (ImGui::RadioButton("Verde", g_selectedPrompt != -1 && g_prompts[g_selectedPrompt].color == "emerald")) { if (g_selectedPrompt != -1) g_prompts[g_selectedPrompt].color = "emerald"; } ImGui::SameLine();
            if (ImGui::RadioButton("Viola", g_selectedPrompt != -1 && g_prompts[g_selectedPrompt].color == "purple")) { if (g_selectedPrompt != -1) g_prompts[g_selectedPrompt].color = "purple"; }

            ImGui::Separator();

            ImGui::InputText("Titolo", g_bufTitle, IM_ARRAYSIZE(g_bufTitle));
            ImGui::InputText("Progetto / Cartella", g_bufProject, IM_ARRAYSIZE(g_bufProject));
            ImGui::InputText("Tag (separati da virgola)", g_bufTags, IM_ARRAYSIZE(g_bufTags));

            ImGui::Separator();
            ImGui::Text("Testo del Prompt:");
            ImGui::InputTextMultiline("##Content", g_bufContent, IM_ARRAYSIZE(g_bufContent), ImVec2(-1, -50));

            // BOTTONI DI AZIONE NOTA
            if (ImGui::Button("Salva", ImVec2(70, 32))) {
                Prompt p;
                if (g_selectedPrompt == -1) {
                    p.id = std::to_string(GetTickCount64());
                    p.color = "default";
                } else {
                    p.id = g_prompts[g_selectedPrompt].id;
                    p.color = g_prompts[g_selectedPrompt].color;
                }
                p.title = g_bufTitle;
                p.project = g_bufProject;
                p.tags = g_bufTags;
                p.content = g_bufContent;

                SavePromptToDisk(p);
                LoadPromptsFromDisk();
                ShowToast("Salvato!");
            }

            ImGui::SameLine();
            if (ImGui::Button("Copia", ImVec2(70, 32))) { // Tasto Copia ridotto
                ImGui::SetClipboardText(g_bufContent);
                ShowToast("Copiato!");
            }

            ImGui::SameLine();
            if (ImGui::Button("Esporta MD", ImVec2(100, 32))) { // Tasto Esporta MD Singolo
                Prompt p;
                p.title = g_bufTitle;
                p.project = g_bufProject;
                p.tags = g_bufTags;
                p.content = g_bufContent;
                p.color = (g_selectedPrompt != -1) ? g_prompts[g_selectedPrompt].color : "default";
                ExportSinglePromptMD(p);
            }

            if (g_selectedPrompt != -1) {
                ImGui::SameLine();
                if (ImGui::Button("Elimina", ImVec2(80, 32))) {
                    std::string filePath = DATA_DIR + "/" + g_prompts[g_selectedPrompt].id + ".txt";
                    fs::remove(filePath);
                    g_prompts.erase(g_prompts.begin() + g_selectedPrompt);
                    g_selectedPrompt = -1;
                    strcpy(g_bufTitle, "");
                    strcpy(g_bufProject, "");
                    strcpy(g_bufTags, "");
                    strcpy(g_bufContent, "");
                    ShowToast("Eliminato.");
                }
            }
        } else {
            ImGui::TextDisabled("Seleziona un prompt a sinistra oppure creane uno nuovo.");
        }
        ImGui::EndChild();

        // Toast
        if (g_toastTimer > 0.0f) {
            g_toastTimer -= ImGui::GetIO().DeltaTime;
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 220, ImGui::GetIO().DisplaySize.y - 50));
            ImGui::SetNextWindowSize(ImVec2(200, 35));
            ImGui::Begin("Toast", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", g_toastMessage.c_str());
            ImGui::End();
        }

        ImGui::End();

        // Rendering
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

// Inizializzazione DirectX11
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
