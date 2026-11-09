# sk700lcd

Sudokoo SK700V（CPUクーラー）の液晶に CPU 温度を送るだけの軽量常駐プログラム。
純正アプリ MasterCraft（Electron＋HWiNFO センサーサービス、常駐 約550MB）の代替。

- C 言語 / CRT なし / Win32 API のみ。exe 約 33KB
- Windows サービスとして動作（ログオン前から表示、ウィンドウなし、UAC なし）
- **[PawnIO](https://pawnio.eu/) ドライバが必須**（下記「必要なもの」）
- 対応 CPU
  - AMD Ryzen（Zen〜Zen5、family 17h/19h/1Ah）… Ryzen 7 5800X で動作確認済み
  - Intel Core（Nehalem 以降）… 実装済み・単体テスト済み、**実機未確認**

## 必要なもの

| | 必要か | 備考 |
|---|---|---|
| **PawnIO ドライバ** | **必須** | CPU 温度・消費電力・クロックを読むのに使う |
| SK700V（USB 接続） | 必須 | Windows 標準の HID ドライバで認識される。専用ドライバ不要 |
| HWiNFO | 不要 | センサーは PawnIO 経由で CPU から直接読む |
| MasterCraft | 不要 | アンインストール可（下記の注意を参照） |
| .NET / Visual C++ ランタイム | 不要 | Windows 標準の DLL（kernel32 / advapi32 / hid / cfgmgr32）のみ使用 |

### PawnIO のインストール

PawnIO は署名済みのカーネルドライバで、ハードウェアモニター系ソフトが共通で使う。
LibreHardwareMonitor（0.9.5 以降）や FanControl などを入れたことがあれば、すでに入っている場合がある。

どちらかの方法で入れる（管理者権限が必要）。

```bat
winget install namazso.PawnIO
```

または [pawnio.eu](https://pawnio.eu/) から `PawnIO_setup.exe` をダウンロードして実行。

入っているかの確認:

```bat
sc query PawnIO
```

`TYPE : 1 KERNEL_DRIVER` と `STATE` が表示されれば導入済み。`FAILED 1060`（サービスが存在しない）なら未導入。

- PawnIO がないとサービスは動くが温度が取れず、液晶には何も送られない。`sk700lcd.exe run` では「PawnIO を開けません」と表示される
- sk700lcd を使っている間は PawnIO をアンインストールしないこと
- PawnIO 本体はこのリポジトリに同梱していない。カーネルドライバは管理者権限で Windows にインストールする必要があり、同梱してもダウンロードの手間が省けるだけのため。公式の配布物を使えば更新も受けられ、同じ PawnIO を使う他のソフトと版が食い違うこともない
- CPU ごとの読み取りモジュール（`res/*.bin`、数 KB）は exe に埋め込み済みなので、別途用意する必要はない

### MasterCraft をアンインストールする場合の注意

- 液晶の設定（表示モード・明るさ・画面の向き・表示項目）はクーラー本体に保存されているので、アンインストール後もそのまま表示される
- ただし設定を変えるには MasterCraft が必要。変える可能性があるならインストーラーを残しておく
- 一時的に MasterCraft を入れ直して設定を変えるときは、先にサービス `SK700LCD` を止める（二重送信になるため）

## 使い方

管理者として実行（exe にマニフェストが入っているので UAC が出ます）。

```bat
build\sk700lcd.exe install    :: C:\Program Files\SK700LCD\ にコピーしてサービス登録・開始
build\sk700lcd.exe uninstall  :: サービス停止・削除
build\sk700lcd.exe run        :: コンソールで実行（動作確認用、Ctrl+C で終了）
build\sk700lcd.exe test 20 log.txt  :: 20 秒実行してログをファイルへ
```

`run` / `test` の最初の行に CPU の識別結果（ベンダー・family・model・ベースクロック、Intel なら TjMax と温度の取得方法）が出ます。

MasterCraft と同時に動かすと液晶への送信が二重になるので、MasterCraft は止めること。

- 自動起動: `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` の `MasterCraft`（値 `C:\MasterCraft\MasterCraft.exe -autoRun`）
- センサーサービス: `MasterCraft Helper Service`（自動起動）

## 実測（Ryzen 7 5800X / Windows 11, 2026-11-09）

| | MasterCraft 1.0.5 | sk700lcd |
|---|---|---|
| プロセス | UI 5 + センサーサービス 2 | 1（サービス） |
| メモリ | 約 545 MB | ワーキングセット 1.1 MB / プライベート 0.45 MB |
| CPU | 常時数 % 変動 | 平均 0.000 %（10 秒計測） |
| サイズ | 本体 162 MB | 33 KB |

## MasterCraft に戻す

管理者の PowerShell で:

```powershell
& "C:\Program Files\SK700LCD\sk700lcd.exe" uninstall
Set-Service -Name 'MasterCraft Helper Service' -StartupType Automatic
Start-Service -Name 'MasterCraft Helper Service'
Set-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name MasterCraft -Value 'C:\MasterCraft\MasterCraft.exe -autoRun'
```

## ビルドとテスト

Visual Studio 2022 Build Tools（MSVC）が必要。

```bat
build.bat              :: build\sk700lcd.exe を作る
tools\run_tests.bat    :: レジスタ解読とフレーム生成の単体テスト
```

単体テストは、Intel のレジスタ解読（TjMax・温度・倍率・電力単位）と、MasterCraft が実際に送っていたフレームとの一致を確認する。
IntelMSR モジュールは Intel CPU 以外では読み込みを拒否するので、Intel 経路の実機確認は Intel 機で `sk700lcd.exe test 20 log.txt` を実行して行う。

## プロトコル（解析結果）

USB HID, VID `381C` / PID `0003`（製品名 `SK700V`）、出力レポート 64 バイト、レポート ID `0x10`。
MasterCraft は 1 秒ごとに次の 2 フレームを送っているだけ（初期化コマンドなし。表示モード等の設定はデバイス側に保存済み）。

```
フレーム: 10 | 68 01 09 <len> <data…> <sum> 16   （以降 64 バイトまで 0 埋め）
          sum = 0x68 から data 末尾までのバイト和 (下位 8bit)

A: data = 03 01                               （固定）
B: data = 01 02 00 PW PW2 00 TT TT TT TT US MH ML
   PW   = CPU 消費電力 [W]               ceil
   PW2  = round(PW × 0.94)
   TT   = CPU 温度 [℃] を ceil した値の float32 ビッグエンディアン
   US   = CPU 使用率 [%]                ceil
   MH ML= CPU クロック [MHz] uint16 BE   ceil（全コア中の最大）
```

値の対応は MasterCraft 内部のセンサー JSON（HWiNFO 由来）と HID 送信内容を同時に記録して確定した。
MasterCraft のセンサーサービスは HWiNFO の項目名で値を拾っている（温度: AMD は `CPU (Tctl/Tdie)`、Intel は `CPU Package`。電力: `CPU Package Power`。使用率: `Total CPU Usage`）。

## センサーの読み方

| 値 | AMD (AMDFamily17) | Intel (IntelMSR) |
|---|---|---|
| CPU 温度 | SMN `0x59800` `[31:21] × 0.125`、bit19 が立っていれば −49（= Tctl） | TjMax（`0x1A2` `[23:16]`）− パッケージ温度（`0x1B1` `[22:16]`）。非対応 CPU は各コア `0x19C` の最大値 |
| 消費電力 | `0xC001029B` の差分 × `0xC0010299` の単位 | `0x611` の差分 × `0x606` の単位（Silvermont/Airmont は µJ 単位） |
| クロック | 各コア `0xC0010293` の倍率 × ベースクロック | 各コア `0x198` の倍率 × ベースクロック |
| ベースクロック | TSC 実測 ÷ P0 倍率（`0xC0010064`） | TSC 実測 ÷ 定格倍率（`0xCE` `[15:8]`） |
| 使用率 | `GetSystemTimes` | 同左 |

AMD の SMN アクセスは HWiNFO / LibreHardwareMonitor と共通の `Global\Access_PCI` ミューテックスで排他している。

## 再解析（tools/analysis）

MasterCraft やファームウェアの更新でプロトコルが変わった場合用。

- `asar_extract.js <app.asar> <outdir> [regex]` … asar 展開
- `hid_sniff.js <logfile> <秒>` … MasterCraft を `--inspect-brk` で起動し、node-hid の送信とセンサー JSON（名前付きパイプ）を記録
- `burn.js <ms>` … 全スレッド負荷（値の対応確認用）
- `../probe_temp.c` … PawnIO で Tctl / CCD / 消費電力を 1 秒ごとに記録（要管理者、AMD）
- `../hid_check.ps1` … 非管理者で HID の出力レポート長を確認し、固定フレームを 1 回送る

## ライセンス表記

`res/AMDFamily17.bin` / `res/IntelMSR.bin` は [PawnIO.Modules](https://github.com/namazso/PawnIO.Modules)（LGPL-2.1）の署名済みモジュール（LibreHardwareMonitor 同梱版をそのままコピー）。全文は [res/LICENSE-PawnIO.Modules.txt](res/LICENSE-PawnIO.Modules.txt)。
