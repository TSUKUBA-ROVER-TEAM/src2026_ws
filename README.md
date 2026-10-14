# ROS 2 Project Template

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
