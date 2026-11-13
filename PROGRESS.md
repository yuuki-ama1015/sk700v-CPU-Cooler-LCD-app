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
- 2026-11-10 GitHub がライセンスを「Other」と判定していたため、LICENSE を MIT の原文だけにした（res/ の PawnIO モジュールが LGPL-2.1 である注記は README の「ライセンス」節と res/LICENSE-PawnIO.Modules.txt に残る）。公開済み v0.1.0 の zip 内 LICENSE.txt は注記付きのままだが内容に矛盾はないので差し替えない
- 2026-11-10 uninstall.bat を実機で確認: exe の無いフォルダから実行し、インストール先 exe 自身による削除の経路を通した。12 秒以内にサービス SK700LCD・C:\Program Files\SK700LCD（exe とフォルダ）・HKLM\SOFTWARE\SK700LCD がすべて消え、PawnIO は残った。PendingFileRenameOperations に予約した再起動時削除が残る（入れ直すと再起動で exe が消える問題があり、11-11 に修正）
- 2026-11-10 v0.1.1 の配布物を作成（未公開・未マージ、ブランチ claude/pawnio-auto-install）。install.bat の PawnIO 導入を winget から「公式リリースの PawnIO_setup.exe を直接ダウンロード → 署名確認 → `-install -silent`」（dist\install_pawnio.ps1）に変更。あわせて: zip を展開せずに実行したら案内して終了、PawnIO の有無は「サービスが RUNNING か」で判定、インストーラーが 3010 を返すか入ったのに動いていなければ再起動を案内、管理者判定を `net session` から `fltmc` に変更（Server サービスが止まっている PC で昇格を繰り返さないため。再昇格は 1 回だけ）、昇格時のパスを環境変数で渡す（空白・括弧・アポストロフィを含むパスで確認）、文字列検索を System32 の findstr のフルパスに変更（PATH 上の GNU find と取り違えないため）
- 2026-11-10 確認できたこと: PawnIO の分岐 7 通りをスタブで実行して期待どおり（動作中 / 無し→導入 / 3010 / 失敗 / 停止中のまま / 停止中→動作 / 終了 0 だがサービス無し）。Windows PowerShell 5.1 でのダウンロード（1 秒、3.4MB）と署名確認（Valid、O=namazso）。展開せずに実行したときの案内。単体テスト 49 件
- 2026-11-10 この PC の PawnIO を `PawnIO_setup.exe -uninstall -silent`（2.2.0 のインストーラー）で外そうとしたところ、終了コード 0 で「アプリ一覧の登録」と「デバイス ROOT\PAWNIO\0000」は消えたが、ドライバは再起動まで読み込まれたまま残った（setupapi.dev.log: Query-removal was vetoed, PNP_VetoLegacyDevice / Setting needs reboot）。サービス PawnIO も RUNNING のまま。PawnIO は再起動しないと外れない、という点が分かった
- 2026-11-11 再起動したところ、PawnIO が止まったうえ、入れ直していた sk700lcd.exe も消えていた。原因は 2026-11-10 の uninstall 確認で予約された再起動時削除（PendingFileRenameOperations）が、入れ直した後も残っていたこと（11-10 の「再起動時に何も起きない」は誤り）。`install` がこの予約（exe とフォルダの削除）を取り消すように修正
- 2026-11-11 PawnIO も sk700lcd も無い状態で、作り直した sk700lcd-0.1.1.zip を展開して install.bat を実行（ユーザー操作）。PawnIO 2.2.0 を公式リリースからダウンロード→署名確認→インストール→サービス開始まで通った（PawnIO・SK700LCD とも RUNNING、一時ファイルは残らず、WS 1.1MB）
- 2026-11-11 予約取り消しを確認: インストール先 exe で uninstall → PendingFileRenameOperations に exe とフォルダの 2 件 → 新しい exe で install →「再起動時の削除予約を取り消しました」と表示され 0 件、サービス RUNNING
- 2026-11-11 v0.1.1 を main にマージし、GitHub Releases に公開（ユーザー承認済み）
- 2026-11-11 **液晶が真っ暗（バックライトも消灯）のまま点かない**問題を調査中。ユーザーによると再起動後〜install.bat 実行前から真っ暗で、v0.1.1 導入後も点かない。分かったこと:
  - sk700lcd 自体は正常: `test 10` で温度・電力・クロックを読め（PawnIO 2.2.0 で問題なし）、HID 送信も 10/10 成功（LCD:OK）。USB 機器（VID_381C PID_0003、親は Genesys GL850G ハブ）も正常で D0
  - `set screen off` → 5 秒 → `set screen on` を 3 回試したが、まったく変化なし（「表示中のまま止まっている」説は外れ）
  - イベントログ: この PC は 11/09 04:59〜10:10（MasterCraft 使用中）、11/10 03:41〜06:27、11/10 17:20〜21:19、11/11 04:12〜07:44（いずれも sk700lcd 使用中）にスリープ。11/11 08:45 と 08:47 に Windows Update の自動再起動、10:56 にユーザーの再起動
  - 液晶が点いているのを最後に目で確認したのは 11/09 21:27（settings demo）。11/09 のスリープ（MasterCraft 使用中）のあとは点いていた。消えたきっかけの候補は ①11/09 22:37 の MasterCraft アンインストール ②11/10 03:41 の最初のスリープ（sk700lcd 使用中）③11/10 09:16 の uninstall 確認（数分間送信が止まった）。11/10 以降に点いているのを確認した記録はない
  - USB の Device Parameters に `DeviceSelectiveSuspended = 1`、`EnhancedPowerManagementEnabled = 1`、`SelectiveSuspendEnabled = 00`
  - MasterCraft のメイン処理（bytenode の index.jsc）には `powerMonitor: suspend` / `powerMonitor: resume` のログと、SK700 まわりに `clearDeviceState` / `writeDeviceState` / `screen-control` / `screen-data` の文字列がある。中身はバイトコードで読めない。これまでのキャプチャ（MasterCraft の起動と設定変更）で HID に送っていたのは A/B フレームだけ。スリープ・復帰時と終了時の送信は未確認
  - 過去に一度だけ、シリアル番号なしで列挙された同じ機器（USB\VID_381C&PID_0003\7&287AA008&0&3、11/08 00:35）がある。11/08 01:37 以降はシリアル番号付き（0FCD894F05A7）
- 2026-11-11 ユーザーが PC の電源を完全に切った（シャットダウン → 電源ユニットのスイッチを切る → 30 秒 → 起動）ところ、Windows のログイン画面（sk700lcd サービスの開始時）から液晶が点いた。sk700lcd の送信内容は正しく、液晶側が固まっていただけと判明。再起動では USB の電源が切れないので直らなかった
- 2026-11-11 MasterCraft のスリープ時の動きを記録（インストーラーから展開した MasterCraft を `tools/analysis/power_probe.js` で起動し、main プロセスで `powerMonitor.emit('suspend')` / `emit('resume')`）。suspend で即 `HID.close()`（ログに「0FCD894F05A7 SK700 Method not implemented.」= SK700 用の休止処理は未実装で、閉じるだけ）、resume の約 1.3 秒後に機器を探し直して `new HID()`（初期化で送るものは無し）。sk700lcd はスリープ通知（PBT_APMSUSPEND）を処理しておらず、スリープの瞬間まで送り続けていた。11/09 のスリープ（MasterCraft 使用中）は無事で、11/10 03:41 の最初のスリープ（sk700lcd 使用中）以降に消えたことと合う
- 2026-11-11 v0.1.2: サービスが PBT_APMSUSPEND を受けたら送信を止め、送信中のフレームが終わるのを待ってから（クリティカルセクションで排他）HID を閉じる。復帰（PBT_APMRESUMEAUTOMATIC / RESUMESUSPEND）では 3 秒待ってから開き直して再開。復帰通知を取りこぼしたときの保険として、起きている間に 60 秒たったら再開。README と配布物の README.txt に、スリープ時の動きと「液晶が真っ暗のままなら電源を完全に切る」を追記。ビルド・単体テスト 49 件 OK。**実機のスリープではまだ未確認**
- 2026-11-11 MasterCraft を一時的に動かして作られた %APPDATA%\MasterCraft（ログ・キャッシュ 1.1MB）は scratchpad に移した。自動起動の登録やサービスは作られていなかった
- 2026-11-11 v0.1.2 をこの PC に入れ、ユーザーが 18:36 ごろスリープを試したところ、**復帰できなかった**（マザーボードの DRAM ランプが点いたまま。電源ユニットのスイッチを切って起動し直した。Kernel-Power 41: SleepInProgress=4 = S3）。この PC の過去 2 か月のログではスリープ 88 回のうち S3 からの復帰失敗はこれが初めて（ほかの不正終了 20 回ほどは SleepInProgress=6 = シャットダウン中）。sk700lcd を入れてからのスリープ 3 回（11/10 03:41、17:20、11/11 04:12）は復帰できていた。DRAM ランプはファームウェアのメモリ初期化段階での失敗で、sk700lcd（Windows 内で HID を閉じるだけ）が直接かかわれる段階ではないが、同じ日に Windows Update（08:45/08:47 の再起動）、PawnIO 2.2.0、sk700lcd 0.1.2 が重なっているので無関係とは言い切れない。復帰しなかったので液晶の修正は確認できていない
- 2026-11-11 22:04、23:37 にもスリープから復帰できず（Kernel-Power 41 SleepInProgress=4、計 3 回連続。この時点で入っていたのは記録なしの v0.1.2）
- 2026-11-13 SK700LCD サービスを止めた状態でスリープ → 復帰できた（11:57:53 スリープ、11:59:11 に USB キーボードで復帰。1 回だけ画面がすぐ戻らず、もう一度キーを押して表示）。→ sk700lcd 動作中のスリープが原因と判断。11/10 までは PawnIO 2.1.0 を開いたままでも復帰できていたので、11/11 の PawnIO 2.2.0（PnP デバイスになった）を開いたままにしていることが最有力
- 2026-11-13 スリープ前に液晶だけでなく PawnIO も閉じ、センサーの読み取りも止めるように変更（tick の読み取り〜送信全体をクリティカルセクションで囲み、suspend 時に hid_close + pio_close。復帰 3 秒後に pio_init からやり直す）。ビルド・単体テスト 49 件 OK。実機のスリープは未確認
- 2026-11-13 ユーザーの依頼でコードを読み直して改善（実機のスリープは未確認）:
  - PawnIO ドライバ 2.2.0 のソース（driver.cpp / callbacks.cpp）を確認。スリープ・復帰時にドライバ自身は何もしない（電源 IRP を下へ流すだけ、電源コールバックなし）。ハンドルを開いているだけでは差が出ないはずで、復帰失敗の正確な原因は不明のまま。「サービス停止で復帰できた」は 1 回だけの結果
  - スリープの通知から接続を閉じるまでを速くした: これまで通知側は tick 全体の終了を待っていた（PawnIO の初期化 + 全センサー読み取り + HID 書き込み最大 1 秒 × 2）。通知が使える時間は約 2 秒しかないので、tick がデバイスに触る前に毎回 `g_suspended` を見て抜けるようにし、HID 書き込みの上限も 1000ms → 400ms にした
  - 復帰の通知は 2 回届くことがある（AUTOMATIC のあと、ユーザー操作があれば RESUMESUSPEND）。2 回目でも液晶を閉じて 3 秒待っていたので、最初の 1 回だけ処理するようにした
  - スリープ時に `g_temp = -1` にして、復帰後に読み直せるまで古い温度を送らない。温度が 5 回続けて読めないときも送信を止めて PawnIO を開き直す（これまでは最後の値を送り続けていた）
  - PawnIO を開いた直後の 1 回は送らない（復帰後の最初のフレームで消費電力が古い値になるのを避ける）
  - 復帰の通知が来なかったときの保険を 60 秒 → 120 秒（スリープに入るまでが長い PC で、スリープ前に開き直さないため）
  - `run` / `test`（コンソール実行）でも `PowerRegisterSuspendResumeNotification`（powrprof.dll を System32 から動的に読み込む。サービスでは読み込まない）で同じ処理をする。これまでは通知を受けず、実行中にスリープすると危険だった。**この経路は実機のスリープで未確認**
  - 状態を `power_event` / `gate_tick` / `read_sensors` に分け、単体テストを 19 件追加（スリープ中は触らない、復帰後 3 秒待つ、2 回目の復帰通知は無視、復帰通知なしでも 120 秒後に再開、待ち時間中の再スリープ、温度が読めないときの扱い）。計 68 件 OK
  - 記録に `LastSuspendWaitMs`（通知から接続を閉じ終わるまでの ms）を追加
- 2026-11-11 確認用に、サービスがスリープ・復帰の時刻を記録するようにした（`LastSuspend`（+ `SuspendCount`）/ `LastResume` / `LastResumeSend` を HKLM\SOFTWARE\SK700LCD に書き、`config` で表示。LastSuspend は RegFlushKey ですぐディスクへ）。ビルド・単体テスト 49 件 OK
- 2026-11-13 **最新の v0.1.2 で、サービス動作中のスリープ → 復帰に成功。液晶も正常に表示**（ユーザーが目視。インストール前の `test 4` も 4/4 送信成功、コンソールでの通知登録も成功）。記録: スリープ通知の処理 12:14:26.003（接続を閉じるまで 0ms）、復帰通知 12:15:13.790、復帰後の最初の送信 12:15:18.050（通知の 4.3 秒後）。イベントログでも 12:14:26 スリープ → 12:15:13 に USB キーボードで復帰。1 回だけの結果

- 2026-11-13 v0.1.2 を main にマージし、GitHub Releases に公開（ユーザーの指示）。v0.1.0 / v0.1.1 のリリースノートに、スリープの不具合と v0.1.2 への案内を追記

## 次にやること
2. スリープをあと数回（長時間のスリープも）試して、復帰と液晶表示が安定しているか確かめる。成功は 11/13 の 1 回だけ。復帰失敗の正確な原因は不明のまま（PawnIO ドライバ自体はスリープ時に何もしない）なので、また失敗したら `sk700lcd config` の記録（`LastSuspend` があるか）で切り分ける
3. 送信を数分止めた場合（アンインストール・更新など）にも液晶が固まらないか確認（11/10 の uninstall 確認は、すでにスリープで固まった後だった可能性が高く、判断材料にならない）
4. Intel 実機で `sk700lcd.exe test 20 log.txt` を実行し、温度・電力・クロックが HWiNFO と一致するか確認
5. PawnIO を一度も入れたことのない PC（ドライバストアに古い版が残っていない状態）でも install.bat を確認できるとなおよい

## 未解決の問題・メモ
- Intel 経路は実機未確認（IntelMSR モジュールは Intel 以外では読み込みを拒否するため AMD 機では試せない）
- Intel のパッケージ温度は LHM / Linux coretemp と同じく bit31（Reading Valid）を確認している。立たない CPU があればコア温度にフォールバックする
- PawnIO は同梱しない（カーネルドライバは要インストールで、同梱しても手間が減るだけ。公式配布なら更新され、他ソフトとの版の食い違いも起きない）。代わりに install.bat が公式リリースの最新版をダウンロードして入れる。winget は使えない PC があるので使わない
- PawnIO_setup.exe のソースは公開されていない（GitHub の PawnIO.Setup は README のみ）。`-install -silent` / `-uninstall -silent` と成功コード 3010 は winget のマニフェスト（namazso.PawnIO 2.2.0）による
- PawnIO をアンインストールしても、ドライバは再起動まで動き続ける。再起動前は `sc query PawnIO` が RUNNING を返すので、install.bat は「導入済み」と判定する（再起動後に表示が止まる）。README のアンインストール節にこの注意を書くか検討
- 64 論理プロセッサを超える CPU（プロセッサグループ 2 以上）はグループ 0 のコアしか見ていない
- PW2 の意味は不明（floor(PW × 0.9529) ≒ PW ÷ 1.05 で記録 24 組すべてに一致、という数値の対応のみ）
- 画面オフ時のフレーム B の固定値（100W / 50W / 60.0 / 40% / 3600MHz）の意味は不明。MasterCraft と同じものをそのまま送っている
- 2026-11-09 18:12 にユーザーが MasterCraft を手動起動しており、SK700LCD と同時に動いていた（センサー値が届かないので送信はしていなかったとみられる）。解析のため Claude が終了させた
- 華氏表示（MasterCraft の temperatureDisplay）は未対応
- MasterCraft を手動で起動すると二重送信になる
