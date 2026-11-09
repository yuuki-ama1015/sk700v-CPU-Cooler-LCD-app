# 進捗と方針

## 今後の実装方針
- C 言語・CRT なし・Win32 API のみで書く（exe とメモリを最小化）。ビルドは MSVC（`build.bat`）
- Windows サービス（LocalSystem、必要特権のみ）として常駐。`install` / `uninstall` / `run` / `test` サブコマンド
- CPU センサーは PawnIO ドライバ＋埋め込みモジュール（DLL 不要、デバイスへ直接 DeviceIoControl）
  - AMD: `AMDFamily17.bin`、Intel: `IntelMSR.bin`。CPUID のベンダーで切り替え
  - 温度は MasterCraft（HWiNFO）に合わせる: AMD = Tctl、Intel = CPU Package
- レジスタ解読・フレーム生成は純粋関数にし、`tools/test_decode.c` で単体テストする（Intel 経路は AMD 機で実行できないため）
- 液晶へは HID 出力レポートを 1 秒ごとに 2 フレーム（プロトコルは README 参照）。失敗時は自動で再接続
- 値の丸めは MasterCraft と同じく切り上げ
- 液晶の設定（温度警告・画面オン/オフ）は sk700lcd 側で持つ（本体は保存しない）。HKLM\SOFTWARE\SK700LCD に DWORD で保存し、`set` 実行時にサービス制御コード 128 を送って再読み込みさせる。温度は ℃ 固定（℉ はユーザーが不要と判断）
- MasterCraft の解析は `tools/analysis/mc_inspect.js` に集約（インスペクタ経由。ipcMain ハンドラを直接呼べば UI 操作なしで再現できる）
- 昇格して実行する PowerShell スクリプトは ASCII のみで書く（PS 5.1 が BOM なし UTF-8 を CP932 で読み、日本語コメントが次の行を飲み込むため）
- AI の開発ルールは AGENTS.md に置く（CLAUDE.md は `@AGENTS.md` のみ）。共通ルールの範囲は開発 PC の ~/.codex/AGENTS.md が正本で、ここへ同期する

## 完了したこと
- 2026-11-09 MasterCraft 1.0.5 を解析。デバイスは Sudokoo SK700V（HID 381C:0003）。Electron の --inspect で node-hid をフックし送信内容を記録、センサー JSON と突き合わせて全フィールドを特定
- 2026-11-09 PawnIO で Tctl を読めることを確認（probe_temp）
- 2026-11-09 sk700lcd.c 実装・ビルド（27KB、警告なし）。非管理者で HID の出力レポート長 64 とフレーム書き込み成功を確認
- 2026-11-09 管理者で 20 秒テスト（20/20 送信成功、温度 60.6〜61.8℃、ベースクロック 99.8MHz）
- 2026-11-09 サービス登録（C:\Program Files\SK700LCD\sk700lcd.exe、自動起動）。MasterCraft の自動起動（HKCU Run）削除、MasterCraft Helper Service を停止・無効化
- 2026-11-09 常駐計測: WS 1.1MB / プライベート 0.45MB / CPU 0.000%（MasterCraft は約 545MB）
- 2026-11-09 Intel 対応を実装（IntelMSR モジュール、パッケージ温度/コア温度、RAPL、IA32_PERF_STATUS）。MasterCraft のセンサーワーカーが Intel では HWiNFO の "CPU Package" を使うことを確認
- 2026-11-09 単体テスト 22 件（AMD/Intel のレジスタ解読、MasterCraft の実送信フレームとの一致）すべて成功
- 2026-11-09 起動直後の 1 回目に消費電力 0W を送っていた問題を修正（初回送信を 1 秒後に）
- 2026-11-09 GitHub リポジトリ（yuuki-ama1015/sk700v-CPU-Cooler-LCD-app）に初回コミット
- 2026-11-09 Intel 対応版をこの PC（Ryzen 7 5800X）に再インストール。15 秒テストで 15/15 送信成功、1 回目から消費電力が出ること（0W なし）を確認。サービス稼働中（WS 1.1MB）
- 2026-11-09 AGENTS.md（複数 AI・クラウド環境共通の開発ルール）と CLAUDE.md を追加
- 2026-11-09 MasterCraft の SK700V 設定画面を解析。SK700V で変えられる設定は 3 つだけ: 画面表示オン/オフ（options.screenStatus）、温度警告オン/オフ（mode.value。オンで 90℃超のとき赤い温度表示が点滅）、℃/℉（アプリ全体の temperatureDisplay）。明るさ・回転・シチュエーションモード等は他機種と共用のストア項目で SK700V の画面には出てこない
- 2026-11-09 解析ツールを整理: `tools/analysis/mc_inspect.js`（インスペクタ起動・HID フック・ipcMain ハンドラ直接呼び出しの共通処理）、`settings_probe.js`（設定を 1 項目ずつ変えて HID 送信を記録し、最後に元へ戻す）。hid_sniff.js も共通処理を使う形に変更
- 2026-11-09 settings_probe で 3 設定を切り替え（いずれも code 0 で成功、元に戻したことも確認）。ただし設定変更の瞬間には HID 送信が一切ない。設定は毎秒の定期送信（フレーム A/B）に反映される作りで、定期送信はセンサー値が届かないと始まらない（Helper Service 無効のため 1 回目は送信ゼロ）
- 2026-11-09 Helper Service を一時起動（SK700LCD は一時停止、終了後に両方とも自動で元に戻した）して settings_probe を実行し、3 設定のプロトコルを特定: 温度警告 = フレーム A の 2 バイト目（03 01 / 03 00）、℉ = フレーム B の 6 バイト目 01 ＋ 温度に ceil(℃)×1.8+32（float 変換は仮数部切り捨て）、画面オフ = フレーム A を送らずフレーム B を固定値 01 00 00 64 32 00 42 70 00 00 28 0E 10 にする。設定は本体に保存されず、PC 側が毎秒送っているだけと判明
- 2026-11-09 PW2 の式を修正: round(PW×0.94) は 22W→20 などで外れていた。記録した全 24 組に一致するのは floor(PW × 0.9524〜0.9534) だけなので floor(PW×0.9529) に変更
- 2026-11-09 `sk700lcd set alert|screen|unit` と `sk700lcd config` を実装。設定は HKLM\SOFTWARE\SK700LCD に保存し、サービス制御コード 128 で動作中のサービスに即反映。単体テスト 53 件すべて成功（MasterCraft が実際に送ったフレームとバイト単位で一致）
- 2026-11-09 設定対応版をこの PC に再インストール（入れたファイルがビルドと同一であることを確認）。`set screen off`→`on`、`set alert off`→`on` を実行し、いずれもサービスへの再読み込み通知が成功。最終設定は 温度警告オン・画面オン・℃。サービス WS 1.16MB。℉ はユーザーが不要とのことで試していない
- 2026-11-09 ユーザーが目視確認: `set screen off` で液晶が消え、`set screen on` で戻った。MasterCraft なしで設定まで完結することを実機で確認
- 2026-11-09 README を更新: 設定コマンド、プロトコル（温度警告・℉・画面オフ・PW2）、「MasterCraft は不要」。以前の「設定はクーラー本体に保存される」は誤りだったので訂正
- 2026-11-09 README に「必要なもの」を追加: PawnIO 必須（winget `namazso.PawnIO` / pawnio.eu、`sc query PawnIO` で確認）、HWiNFO・MasterCraft・ランタイム不要、MasterCraft アンインストール時の注意。PawnIO は同梱しない方針を明記
- 2026-11-09 この PC から MasterCraft をアンインストール（公式アンインストーラーを `/allusers /S` で実行。Helper Service・ショートカット・%APPDATA%\MasterCraft はアンインストーラーが削除）。残っていた C:\MasterCraft の空フォルダと %LOCALAPPDATA%\mastercraft-updater（340MB）はごみ箱へ移動。インストーラー（Downloads\MasterCraft-1.0.5-setup.exe）は残した。SK700LCD・PawnIO は稼働中
- 2026-11-09 README の「MasterCraft に戻す」を削除し「アンインストール」節に置き換え（MasterCraft の入れ直しは公式インストーラーを実行するだけなので手順書は不要、とのユーザー判断）
- 2026-11-09 `uninstall` が設定レジストリ（HKLM\SOFTWARE\SK700LCD）も削除するように修正。PawnIO は他ソフトと共用なので残す旨を表示。この PC にインストール済みの exe はまだ修正前（設定キーは消さない）。リポジトリの build から `uninstall` を実行すれば消える
- 2026-11-09 ℉ 機能を削除（ユーザーが不要と判断）。`set unit` と関連コード・テストを削除し、温度は常に ℃。README のプロトコル説明には解析結果として ℉ の形式を残した
- 2026-11-09 uninstall を改善: インストール先の exe 自身から実行しても、終了後に cmd.exe が exe とフォルダを削除する（念のため再起動時の削除も予約）。設定キー削除と PawnIO を残す旨の案内も表示。実行中の exe のコピーで、実行中は消えず終了後に exe とフォルダが消えることを確認。README に「残すもの」と削除のされ方の表を追加
- 2026-11-09 単体テスト 49 件すべて成功（℉ のテストを削除し、記録した実フレームを 1 件追加）
- 2026-11-10 配布 zip を作成: `dist\`（install.bat / uninstall.bat / README.txt）と `tools\make_release.ps1`（ビルド・テスト後に `build\sk700lcd-<版>.zip` を作る。bat は CRLF に変換）。install.bat は自己昇格し、PawnIO が無ければ winget（namazso.PawnIO）で導入、MasterCraft が動いている/入っていれば注意を表示してから `sk700lcd.exe install`
- 2026-11-10 sk700lcd-0.1.0.zip（26KB）を展開して install.bat を管理者で実行し、この PC のサービスを最新版に入れ替え（PawnIO 検出 OK、インストール先の exe が zip と同一、サービス稼働、WS 628KB）
- 2026-11-10 GitHub Releases に v0.1.0 を公開（sk700lcd-0.1.0.zip、26KB、ダウンロードしてハッシュ一致を確認）
- 2026-11-10 ライセンスを MIT に決定（ユーザーが一任）。LICENSE を追加（res/ の PawnIO モジュールは対象外で LGPL-2.1 と明記）、README と配布物の README.txt に記載、make_release.ps1 が LICENSE.txt を同梱。v0.1.0 の zip は公開済みの exe のまま LICENSE.txt と更新した README.txt を加えて差し替え（SHA256 1818EF0F62E980FFFD9E02A2817C299C5BD2D25A8BCAE821217908EE6369A39D、ダウンロードして一致を確認）、リリースノートも更新

## 次にやること
1. Intel 実機で `sk700lcd.exe test 20 log.txt` を実行し、温度・電力・クロックが HWiNFO と一致するか確認
2. 再起動・スリープ復帰・USB 抜き差し後も表示が続くか確認（MasterCraft なしの状態で）
3. PawnIO の無い PC で install.bat の winget 自動導入を確認（この PC では PawnIO 導入済みのため未確認）
4. uninstall.bat（インストール先 exe の自己削除を含む）を実際に実行して確認（未実施）

## 未解決の問題・メモ
- Intel 経路は実機未確認（IntelMSR モジュールは Intel 以外では読み込みを拒否するため AMD 機では試せない）
- Intel のパッケージ温度は LHM / Linux coretemp と同じく bit31（Reading Valid）を確認している。立たない CPU があればコア温度にフォールバックする
- PawnIO は同梱しない（カーネルドライバは要インストールで、同梱しても手間が減るだけ。公式配布なら更新され、他ソフトとの版の食い違いも起きない）
- 64 論理プロセッサを超える CPU（プロセッサグループ 2 以上）はグループ 0 のコアしか見ていない
- PW2 の意味は不明（floor(PW × 0.9529) ≒ PW ÷ 1.05 で記録 24 組すべてに一致、という数値の対応のみ）
- 画面オフ時のフレーム B の固定値（100W / 50W / 60.0 / 40% / 3600MHz）の意味は不明。MasterCraft と同じものをそのまま送っている
- 2026-11-09 18:12 にユーザーが MasterCraft を手動起動しており、SK700LCD と同時に動いていた（センサー値が届かないので送信はしていなかったとみられる）。解析のため Claude が終了させた
- 華氏表示（MasterCraft の temperatureDisplay）は未対応
- MasterCraft を手動で起動すると二重送信になる
