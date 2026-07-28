#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <algorithm>

// Header di Dear ImGui
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

namespace fs = std::filesystem;

// Struttura dati per i Prompt
struct Prompt {
    std::string id;
    std::string title;
    std::string project;
    std::string tags;
    std::string content;
};

// Variabili Globali
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

std::vector<Prompt> g_prompts;
int g_selectedPrompt = -1;
char g_searchBuf[128] = "";

// Buffer per l'editor
char g_bufTitle[256] = "";
char g_bufProject[256] = "";
char g_bufTags[256] = "";
char g_bufContent[8192] = "";

const std::string DATA_DIR = "PromptVault_Data";

// Funzioni Helper
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void EnsureDataFolder() {
    if (!fs::exists(DATA_DIR)) fs::create_directory(DATA_DIR);
}

void SavePromptToDisk(const Prompt& p) {
    EnsureDataFolder();
    std::ofstream file(DATA_DIR + "/" + p.id + ".txt");
    if (file.is_open()) {
        file << p.title << "\n" << p.project << "\n" << p.tags << "\n" << p.content;
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
                std::string line, content;
                while (std::getline(file, line)) content += line + "\n";
                p.content = content;
                g_prompts.push_back(p);
            }
        }
    }
}

// Configura lo stile scuro "Obsidian" per l'interfaccia
void SetupObsidianStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                  = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.09f, 0.09f, 0.10f, 1.00f); // #161618
    colors[ImGuiCol_ChildBg]               = ImVec4(0.12f, 0.12f, 0.13f, 1.00f); // #1e1e22
    colors[ImGuiCol_Border]                = ImVec4(0.20f, 0.20f, 0.22f, 1.00f); // #333338
    colors[ImGuiCol_FrameBg]               = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.12f, 0.12f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.39f, 0.40f, 0.95f, 1.00f); // Indigo Accent
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.48f, 0.49f, 0.98f, 1.00f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.32f, 0.33f, 0.85f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.35f, 0.35f, 0.40f, 1.00f);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"PromptVaultClass", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Prompt Vault - Native C++ ImGui", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    SetupObsidianStyle();

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

        // Finestra Principale Fullscreen Integrata
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Prompt Vault", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        // PANNELLO SINISTRA: Lista Prompt
        ImGui::BeginChild("Sidebar", ImVec2(320, 0), true);
        ImGui::TextDisabled("PROMPT VAULT (NATIVO)");
        ImGui::Separator();

        if (ImGui::Button("+ Nuovo Prompt", ImVec2(-1, 30))) {
            g_selectedPrompt = -1;
            strcpy(g_bufTitle, "Nuovo Prompt");
            strcpy(g_bufProject, "");
            strcpy(g_bufTags, "");
            strcpy(g_bufContent, "");
        }

        ImGui::InputText("##Search", g_searchBuf, IM_ARRAYSIZE(g_searchBuf));

        ImGui::Separator();
        ImGui::Text("I Miei Prompt (%d)", (int)g_prompts.size());

        for (int i = 0; i < (int)g_prompts.size(); i++) {
            std::string searchStr = g_searchBuf;
            std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);
            std::string titleStr = g_prompts[i].title;
            std::transform(titleStr.begin(), titleStr.end(), titleStr.begin(), ::tolower);

            if (!searchStr.empty() && titleStr.find(searchStr) == std::string::npos) continue;

            if (ImGui::Selectable((g_prompts[i].title + "##" + std::to_string(i)).c_str(), g_selectedPrompt == i, 0, ImVec2(0, 40))) {
                g_selectedPrompt = i;
                strcpy(g_bufTitle, g_prompts[i].title.c_str());
                strcpy(g_bufProject, g_prompts[i].project.c_str());
                strcpy(g_bufTags, g_prompts[i].tags.c_str());
                strcpy(g_bufContent, g_prompts[i].content.c_str());
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // PANNELLO DESTRA: Editor
        ImGui::BeginChild("Editor", ImVec2(0, 0), true);
        if (g_selectedPrompt != -1 || strlen(g_bufTitle) > 0) {
            ImGui::InputText("Titolo", g_bufTitle, IM_ARRAYSIZE(g_bufTitle));
            ImGui::InputText("Progetto/Cartella", g_bufProject, IM_ARRAYSIZE(g_bufProject));
            ImGui::InputText("Tag", g_bufTags, IM_ARRAYSIZE(g_bufTags));

            ImGui::Separator();
            ImGui::Text("Contenuto del Prompt:");
            ImGui::InputTextMultiline("##Content", g_bufContent, IM_ARRAYSIZE(g_bufContent), ImVec2(-1, -60));

            if (ImGui::Button("Salva Prompt", ImVec2(120, 35))) {
                Prompt p;
                if (g_selectedPrompt == -1) {
                    p.id = std::to_string(GetTickCount64());
                } else {
                    p.id = g_prompts[g_selectedPrompt].id;
                }
                p.title = g_bufTitle;
                p.project = g_bufProject;
                p.tags = g_bufTags;
                p.content = g_bufContent;

                SavePromptToDisk(p);
                LoadPromptsFromDisk();
            }

            ImGui::SameLine();
            if (ImGui::Button("Copia negli Appunti", ImVec2(150, 35))) {
                ImGui::SetClipboardText(g_bufContent);
            }
        } else {
            ImGui::TextDisabled("Seleziona un prompt a sinistra o creane uno nuovo.");
        }
        ImGui::EndChild();

        ImGui::End();

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.09f, 0.09f, 0.10f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
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

// Implementazioni DirectX e gestione Finestra Windows
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
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
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
