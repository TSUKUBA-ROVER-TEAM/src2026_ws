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
arm_1〜4 の位置制御には標準 `joint_trajectory_controller/JointTrajectoryController`、
`arm_y_joint` と `gripper_joint` の速度制御には ForwardCommandController を使います。
位置指令は axis を押している間に積算します。
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

## アームの位置指令範囲

manual_controllerの`arm_123_position_min` / `arm_123_position_max`は、arm_1〜3の
順で位置目標の上下限を指定します。現在の既定値は各軸±12.466370614 radで、
現在の`gear_ratio=1.0`・オフセット0のCAN位置範囲±4πから0.10 rad内側です。
これは機械的な可動域の校正値ではありません。原点・減速比の変更時には、
同じ関節座標系で上下限も設定し直してください。

限界に達すると、その方向への目標の積み増しだけを停止します。
逆操作は受け付け、限界到達時に目標を実角度や反対端へ飛ばしません。
ドライバへ範囲外の位置指令が到達しても、最後の有効な位置目標で保持し、
位置範囲超過だけを理由に全モータの保持を解除しません。
通信途絶や電源断に対する復帰・停止処理は、この変更とは別です。

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

## RobStride 単体試験

RobStride だけを接続する場合は、ITR 側を mock hardware に切り替えます。
このモードでは足回り・arm_y・arm_4・gripper の実機 CAN 送信を行いません。
RobStride の設定対象は arm_1〜3 のままで、arm_3 だけの接続でも操作できます。

```bash
just run real robstride_only:=true
```

arm_3 は `buttons[3]` を押しながら `axes[7]` で操作します。
`arm_position_controller` は標準 JointTrajectoryController で、位置 Command / State Interface を使います。
Recovery Manager が送る軌道点の位置は arm_1〜4 の4要素の有限値です。
未接続の RobStride 軸への送信停止は Hardware 側で行います。

通常の電源断・CAN通信断では RobStride Hardware を ACTIVE のまま保ちます。
一度 ready になった軸が落ちた場合、共有の `arm_position_controller` は全軸停止し、
通信復帰・指令位置の実測位置への同期・Controller 再 activate 後に再開します。
初めから未接続の軸は単体試験の起動を妨げません。

外部位置指令は `/arm_position_controller/position_targets` に `trajectory_msgs/JointTrajectory`
で送ります。`joint_names` は arm_1〜4 の順、`points[].positions` は絶対位置です。
`header.stamp` には指令作成時点の現在の ROS 時刻を入れ、その時刻から軌道を開始します。
`points[].time_from_start` に各点への到達時間を指定します。manual_controller は通常、
0.1秒後の目標位置を1点送ります（`arm_position_trajectory_duration` で変更可能）。
多点軌道も受け付けます。各点の到達時間は非負・昇順、位置などの値は有限値にします。
作成時刻が判別できないゼロ stamp、未来の開始 stamp、終了時刻を過ぎた軌道は受け付けません。
`/arm_position_controller/recovery_ready` が true になった後の新しい指令だけを受け付けます。
復旧前・復旧中の指令は再送せず、現在位置から作り直してください。
`/arm_position_controller/joint_trajectory` への直接送信は Recovery Manager 専用です。
Controller の action への直接送信も Recovery Manager の旧指令破棄を迂回するため使用しません。

Hardware 自体が ERROR になった場合は、Recovery Manager が既存 Lifecycle サービスで
configure → activate し、同時に停止した `joint_state_broadcaster` も再 activate します。
Driver は再 activate 時に古い Interface 使用状態と位置指令を消し、実測位置への同期を
最初からやり直します。電源が戻るまでは位置指令ゲートを閉じたまま待機します。
