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
char g_bufContent[65536] = ""; // Buffer fino a 64KB per prompt lunghi

// Modalità di Visualizzazione (0 = TXT Editor, 1 = Markdown Preview)
int g_viewMode = 0;

// Gestione Filtri
std::string g_activeFilterType = ""; // "project" o "tag"
std::string g_activeFilterValue = "";

// Toast
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
        std::ofstream file(ofn.lpstrFile, std
