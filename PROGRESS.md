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
- 2026-11-09 README に「必要なもの」を追加: PawnIO 必須（winget `namazso.PawnIO` / pawnio.eu、`sc query PawnIO` で確認）、HWiNFO・MasterCraft・ランタイム不要、MasterCraft アンインストール時の注意。PawnIO は同梱しない方針を明記

## 次にやること
1. 液晶に温度が表示されているかユーザーが目視確認
2. Intel 実機で `sk700lcd.exe test 20 log.txt` を実行し、温度・電力・クロックが HWiNFO と一致するか確認
3. 再起動・スリープ復帰・USB 抜き差し後も表示が続くか確認
4. 本体コードのライセンスを決める（リポジトリは公開）
5. `install` 実行時に PawnIO の有無を確認し、未導入なら導入方法を表示する（今は未導入でもサービス登録できてしまい、液晶に何も出ない）

## 未解決の問題・メモ
- Intel 経路は実機未確認（IntelMSR モジュールは Intel 以外では読み込みを拒否するため AMD 機では試せない）
- Intel のパッケージ温度は LHM / Linux coretemp と同じく bit31（Reading Valid）を確認している。立たない CPU があればコア温度にフォールバックする
- PawnIO は同梱しない（カーネルドライバは要インストールで、同梱しても手間が減るだけ。公式配布なら更新され、他ソフトとの版の食い違いも起きない）
- 64 論理プロセッサを超える CPU（プロセッサグループ 2 以上）はグループ 0 のコアしか見ていない
- PW2（PW × 0.94）の意味は不明。数値の対応のみ確認済み
- フレーム A（03 01）の意味は不明（MasterCraft が毎秒送る固定値をそのまま送っている）
- 華氏表示（MasterCraft の temperatureDisplay）は未対応
- MasterCraft を手動で起動すると二重送信になる
