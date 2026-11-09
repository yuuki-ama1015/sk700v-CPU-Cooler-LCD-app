sk700lcd - Sudokoo SK700V の液晶に CPU 温度を表示する軽量常駐ソフト
https://github.com/yuuki-ama1015/sk700v-CPU-Cooler-LCD-app

■ 必要なもの
  - Windows 10 / 11 (64bit)
  - AMD Ryzen または Intel Core の CPU（Intel は動作未確認）
  - PawnIO ドライバ（無ければ install.bat が winget で自動インストール）
  - MasterCraft は不要（入っていればアンインストールして構いません）

■ インストール
  1. ダウンロードした zip を右クリック → プロパティ → 「許可する」にチェック → OK
     （しないと展開後に SmartScreen の警告が出ます。出た場合は「詳細情報」→「実行」）
  2. zip を展開し、install.bat をダブルクリック → UAC で「はい」
  3. 数秒で液晶に CPU 温度が表示されます。展開したフォルダは削除して構いません
     （本体は C:\Program Files\SK700LCD\ にコピーされ、Windows サービスとして自動起動します）

■ 設定（管理者のコマンドプロンプトで）
  "C:\Program Files\SK700LCD\sk700lcd.exe" config            今の設定を表示
  "C:\Program Files\SK700LCD\sk700lcd.exe" set screen on|off  画面表示
  "C:\Program Files\SK700LCD\sk700lcd.exe" set alert on|off   温度警告（90℃超で赤く点滅）

■ アンインストール
  uninstall.bat をダブルクリック → UAC で「はい」
  （または管理者で "C:\Program Files\SK700LCD\sk700lcd.exe" uninstall）
  サービス・本体・設定がすべて削除されます。
  PawnIO は他のソフトも使うので残します。不要なら「設定 → アプリ」からアンインストールしてください。

■ 同梱物
  sk700lcd.exe                 本体（約 37KB）
  install.bat / uninstall.bat  インストール / アンインストール
  LICENSE-PawnIO.Modules.txt   exe に埋め込んだ PawnIO モジュールのライセンス（LGPL-2.1）
