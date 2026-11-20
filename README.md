# MC Debugger

Minecraft 崩潰診斷工具（Qt 6.11.2 / MSVC 2022 / Windows）。
選實例 → 整理 `latest.log`（刪雜訊、遮個資）→ 交給 AI（Groq / 本地 LLM / GitHub Copilot）。

## 建置（Qt Creator）
1. 開啟 `CMakeLists.txt`，Kit 選 **Desktop Qt 6.11.2 MSVC2022 64-bit**。
2. 建置並執行。不需要額外函式庫（只用 Qt Widgets + Network，Windows 另連 `crypt32`）。
3. 發佈：`windeployqt MCDebugger.exe`。

## 三個 AI 管道
| 管道 | 端點 | 認證 |
|---|---|---|
| Groq | `https://api.groq.com/openai/v1/chat/completions` | API Key |
| 本地 LLM | `llama-server`（127.0.0.1 隨機埠）`/v1/chat/completions` | 無 |
| GitHub Copilot | 官方 `copilot` CLI（`copilot -p`，非互動） | 設定內「登入 GitHub」（`copilot login`）或 Token |

### 金鑰加密
Windows DPAPI（`CryptProtectData` + 應用程式專屬 entropy），密文（Base64）存在 `%APPDATA%\MCDebugger\settings.ini`。
只有同一個 Windows 帳號能解開。非 Windows 平台**拒絕儲存**，不會退回明文。

## 設計決策與依據（請自行稽核）
**1. 「QMythos」= Qwythos**
我把你說的 QMythos 解讀為 `empero-ai/Qwythos-9B-Claude-Mythos-5-1M-GGUF`（模型卡：9B 推理模型，推薦 Q4_K_M，5.24 GiB）。
下載網址在「設定 → 本地 LLM → 進階」可改。**若你指的是別的模型，改那個網址即可。**
該模型每次回答前會輸出 `<think>…</think>`，程式會隱藏（也處理只有 `</think>` 的情形）；取樣參數採模型卡建議值 temperature 0.6 / top_p 0.95。
來源：https://huggingface.co/empero-ai/Qwythos-9B-Claude-Mythos-5-1M-GGUF

**2. llama.cpp 下載**
`ggml-org/llama.cpp` 的 `releases/latest` 目前指向「指標 release」`v1.0.0`，裡面只有 `nightly-tag.txt`，內容才是真正版本（實測為 `b12359`）。
流程：HEAD `releases/latest` → 取指標標籤 → 下載 `nightly-tag.txt` → 組出
`llama-<tag>-bin-win-{vulkan|cpu}-x64.zip` / `llama-<tag>-bin-win-cuda-12.4-x64.zip` + `cudart-llama-bin-win-cuda-12.4-x64.zip`。
這四個網址都已實測回 HTTP 200。若 `nightly-tag.txt` 不存在則退回使用轉址標籤。
解壓使用 Windows 內建 PowerShell `Expand-Archive`。

**3. GitHub 管道改為 GitHub Copilot（透過官方 Copilot CLI）**
原本的 GitHub Models 推論 API 已於 2026-07-30 退役。現在把 `copilot` CLI 當子行程執行（`npm install -g @github/copilot`），預設模型 `auto`，shell／寫檔工具一律封鎖；可在設定按「登入 GitHub」直接登入。Copilot 模型 ID 清單是依官方顯示名稱推導、未逐一驗證，可自行輸入任何 ID。

**4. 預填 PAT 連結**
參數 `name`、`description`、`expires_in`、`user_models=read`（Models 的帳號權限）取自 GitHub 文件的 URL 預填表。
**注意**：該段說明在 2026-05 被 GitHub 以「broken FG PAT URL prefill feature」為由從文件移除（github/docs PR #44419），
所以連結可能不會自動帶入欄位；設定頁同時顯示手動步驟（Account permissions → Models → Read-only）。

**5. Log 整理策略**（`LogCleaner.cpp`）
- 解析 vanilla / Forge / Fabric 三種標頭格式；無標頭的行視為前一筆的延續（堆疊追蹤）。
- 丟棄 DEBUG/TRACE 與已知 INFO 雜訊（資源重載、音效初始化、聊天…）。
- 保留：ERROR/FATAL（最高優先）、WARN、含關鍵字的 INFO、前 12 筆（環境資訊）、後 12 筆（崩潰前動作）、錯誤前 3 筆上下文。
- 相同訊息（數字正規化後）合併為 `(×N)`；每段堆疊最多 12 frames，其餘折疊。
- 超出字元上限時依優先級由低到高丟棄；ERROR/FATAL 最後才動。
- 另外萃取 `crash-reports/` 最新報告（主堆疊、Head、System Details 重點、Suspected Mods）與 `hs_err_pid*.log`（`#` 摘要＋原生堆疊）。
- Mod 清單：Fabric/Quilt 的 `Loading N mods:` 壓成一行。
- 遮蔽：Windows/Unix 使用者目錄、accessToken/Session ID、`Setting user:` 玩家名、UUID、`IP:Port`。**無法保證完全匿名，送出前請看預覽。**
- 預設上限 14,000 字元（約 4–5k tokens），因為 Groq 免費層的單次 token 限制較小。

## 已知限制
- 沒有在真實 Windows + Qt 6.11.2 上執行過：我在 Linux（Qt 6.4.2）用 `-Wall -Wextra` 編譯，**零錯誤零警告**，並用模擬 Fabric 崩潰 log 驗證整理器（58 KB → 2 KB）、offscreen 啟動 UI。
  Windows 專屬路徑（DPAPI、PowerShell 解壓、`CREATE_NO_WINDOW`）已寫好但**只通過語法檢查，未實測**。
- Forge/NeoForge 的 latest.log 通常不含完整 Mod 清單；有 crash-report 時會嘗試從 `Mod List:` 區段補。

## 授權
本專案以 **PolyForm Shield License 1.0.0** 釋出（見 `LICENSE`）。
- 可以：使用、修改、散布（散布時須附上授權條款或其網址，以及 `Required Notice:` 行）。
- 不可以：用它來提供**與本軟體競爭**的產品或服務。
- 這是 *source-available* 授權，**不是** OSI 定義的開源授權。
- 條款全文取自 PolyForm 官方文字（https://polyformproject.org/licenses/shield/1.0.0 ；比對來源：ScanCode LicenseDB）。
- 我沒有加 `Licensor Line of Business:` 行（可選）。不加的話，若你日後停止提供某項產品，別人可以開始做與它競爭的東西。

### 第三方元件（請自行確認各自的條款）
- **Qt 6**（Widgets / Network）：以動態連結方式隨 `windeployqt` 散布；Qt 在 LGPLv3 / GPL / 商業授權之間擇一，散布時須遵守你所選方案的義務（例如附上授權文字、讓使用者能替換 Qt 函式庫）。
- **llama.cpp**：由程式在使用者電腦上從官方 releases 下載，不隨本專案散布。
- **Qwythos 模型**：由使用者從 Hugging Face 下載，不隨本專案散布；模型本身有自己的授權，使用前請閱讀模型卡。

> 以上不是法律意見。


## 提示詞規則與模型預設（Groq／GitHub Copilot）

- 設定 → 提示詞規則：**搜尋網路**、**不要猜測（不知道就說不知道）**、**深度思考**，三條都可個別關閉；搜尋規則只在該管道＋模型真的有搜尋工具時才加入。
- Groq 預設 `openai/gpt-oss-120b`、推理強度高、內建 `browser_search`；清單取自你的金鑰可用的所有聊天模型，可隨時改。
- Copilot 預設模型 `auto`，可選其他模型、Auto 路由偏好與推理強度。
