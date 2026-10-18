# SRC2026 workspace

## 実機の足回り

足回りの4台はRobomas基板のU3（FDCAN1、Classic CAN 1 Mbps）へ接続します。
PCのUSB-CANは基板の上位CAN（FDCAN2）へ接続してください。

| 車輪 | joint | U3のESC ID / module_id |
|---|---|---|
| 左前 | drive_left_forward_joint | 1 |
| 右前 | drive_right_forward_joint | 2 |
| 左後 | drive_left_backward_joint | 3 |
| 右後 | drive_right_backward_joint | 4 |

Robomas基板IDの既定値は3、PCのインターフェースは`can0`です。
`module_id`からモータ種類とバスを判定するため、`motor_type`の設定は不要です。
車輪rad/sとモータRPMの変換には`drive_gear_ratio`（既定19.0）を使います。
U5のRobomasはmodule ID 5〜8、DaMiaoは13〜15です。`arm_4_joint`はDaMiao ID1
(module ID 13)、`gripper_joint`はRobomas ID1 (module ID 5)、`arm_y_joint`はRobomas ID2
(module ID 6)に割り当てています。
arm操作はforward command controllerで、`arm_4_joint`を位置制御、`arm_y_joint`と
`gripper_joint`を速度制御します。位置指令はaxisを押している間に積算します。
`arm_4_joint`はaxes[2]を正方向、axes[5]を負方向に使います。未押下の値1.0を基準に
押下量を速度へ変換します。`gripper_joint`はbutton[4]/button[5]で正負に動き、速度は30です。

```bash
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install --packages-up-to src2026_bringup
source install/setup.bash
ros2 launch src2026_bringup real.launch.yaml can_interface:=can0 robomas_board_id:=3
```

実機launchは足回りのcontrollerとジョイスティック操作ノードを起動します。
1軸ずつ確認する場合は、controllerをinactiveで読み込む次のlaunchを使います。

```bash
ros2 launch src2026_bringup actuator_test.launch.yaml robomas_board_id:=3
```

CAN FD・BRSなし・1 Mbpsの上位通信と、命令8のページ形式フィードバックに
対応したRobomas_G474REファームを使ってください。
Robomasの位置フィードバックは未実装のため、実機の位置stateは0のままです。

`build_depends.repos`は今回のCAN更新と既存スワーブ制御を統合した版に固定しています。
依存リポジトリのサーボ補正・スワーブ制御のローカル変更は保持しています。
固定コミットはまだGitHubへpushしていないため、新規取得時は公開後に`vcs import`するか、
このPCの更新済み`dep/itr_control`と`dep/itr_can_protocol`から取得してください。

## 開発環境

ROS 2 Jazzy 向けのプロジェクトテンプレートです。
Dev Container、GitHub Actions、`just` による開発コマンドを含んでいます。

## 前提

- ROS 2 Jazzy
- Ubuntu 24.04 または Dev Container
- `colcon`
- `rosdep`
- `vcs`
- `just`

Dev Container を使う場合は、VS Code でこのリポジトリを開き、コンテナ内で作業してください。

## セットアップ

1. このリポジトリをテンプレートとして新しいリポジトリを作成します。
2. ROS パッケージをこのリポジトリ直下のサブディレクトリとして追加します。
3. ワークスペースをセットアップします。

```bash
just deps
```

`just deps` は、`build_depends.repos` の取得と rosdep による依存パッケージのインストールをまとめて実行します。

## 依存関係の管理

`vcs import` で取得するリポジトリ依存は、`build_depends.repos` にまとめ、
`dep/` 配下へ配置します。
自分たちで管理する基幹ライブラリ、apt で入らない外部 ROS パッケージ、既成ドライバなどが対象です。

```yaml
repositories:
  dep/some_driver:
    type: git
    url: https://github.com/example/some_driver.git
    version: main
```

apt で入るシステム依存は、各 ROS パッケージの `package.xml` に書き、rosdep で解決します。

## よく使うコマンド

```bash
# 利用できるコマンドを表示
just

# 依存関係を取得し、rosdep で依存パッケージをインストール
just deps

# 全パッケージをビルド
just build

# 指定したパッケージだけビルド
just build <package_name>

# テストを実行
just test

# 指定したパッケージだけテスト
just test <package_name>

# launch ファイルを実行
just run <launch_name>

# フォーマット
just format

# 生成物を削除
just clean
```

引数なしの `just test` は、`dep/` 配下の外部依存を除き、プロジェクト自身の
パッケージをテストします。外部依存も明示的に指定すればテストできます。

`just run <launch_name>` は、ワークスペース内の `_bringup` で終わるパッケージを探し、
そのパッケージ内の `<launch_name>.launch.yaml` を実行します。

## 命名規則

このテンプレートでは、パッケージ名を以下の規則にしています。

- bringup パッケージ: `_bringup` で終わる名前
- msg パッケージ: `_msgs` で終わる名前

CI は全パッケージをビルドし、`dep/` 配下の外部依存を除いたプロジェクト自身の
パッケージをテストします。

## フォーマットとテスト

このテンプレートでは `.clang-format`、`setup.cfg`、`.pre-commit-config.yaml` を用意しています。
`just format`、pre-commit、CI は C/C++ に `ament_clang_format`、Python に `isort` と `black` を使います。

```bash
pre-commit install
pre-commit run --all-files
```

CI では ROS 2 Jazzy 環境で依存関係のインストール、フォーマット、ビルド、テストを実行します。

## ディレクトリ構成

```text
.
├── .devcontainer/          # ROS 2 Jazzy 用 Dev Container
├── .github/workflows/      # GitHub Actions CI
├── dep/                    # vcs import で取得する外部リポジトリ依存
├── <package>/              # ROS パッケージ
├── build_depends.repos     # vcs import 用の外部リポジトリ依存
├── justfile                # 開発コマンド
├── .clang-format           # C/C++ フォーマット設定
└── .pre-commit-config.yaml # pre-commit 設定
```

## テンプレート利用時に見直すもの

- パッケージ名
- `build_depends.repos` の依存リポジトリ
