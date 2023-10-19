# b4_research
卒業論文用リポジトリ

## Title(temp)
プロセス状態遷移のログを利用したスケジューラのモニタリング

## Abstract
オペレーティングシステムにおけるスケジューラは, クラッシュやデッドロックのような明確な障害を伴わないため, 意図した通りに CPU 時間の割り当てが行われているかどうかを確認することが難しい. 本研究では, スケジューリングイベントが発生した際のプロセス状態を時系列順に記録することで, 各コアで実行されるプロセスの遷移をモニタリングする手法を提案する. また, 教育用オペレーティングシステム xv6 において, プロセス状態について収集したログを元にスケジューラの問題点の発見からFairness 改善を行うまでの一連の流れを示す.

## Implementation
以下に示すような Kernel の拡張を行った
### Scheduler
- Multiple runrueue に対応した scheduler の追加
+ Work Stealing の実装

### Logging
測定用に, 2つ system callを追加
- bufwrite
bufwrite.c を実行し, ログをバッファに自動的に記録する
- bufread
bufwrite system call で記録されたログを print する

### Workloads
測定時に実行するワークロードも実装
- CPU-intensive なタスク
- I/O-intensive なタスク
- 上記の混合タスク
- xv6 の fairness を崩すようなタスク
など

## Discussion
### CPU-intensive なタスクについて
- プロセスの個数が 1 〜 2 のときは, CPU 2, 3 が特に使われていない
- プロセス数 4 のとき, CPU が一切入れ替わることなかった (理由は調査中)
- プロセス数 5 以上の時は動作に致命的な問題点は見られなかった

### I/O-intensive なタスクについて
- CPU-intensive なタスクとは異なりI/O を発行しているので, 各コアで常にプロセスが RUNNING で動いていることは起きず, 細切りのグラフ形状になった
- pid 3 は I/O 処理を行わないにも関わらず RUNNING 状態が長く続いていることが読み取れるが, これは fork を自分一人で全て行っているためであると考えられる
- SLEEPING から復帰した際にも, 前と同じコアで処理が継続するケースが多く見られた
+ CPU-intensive の処理の場合にはこのような現象は見られなかった.

### CPU-intensive, I/O-intensive なタスクを同時に実行した際の挙動
- コアを跨ぐことはあるものの, CPU-intensive なタスクは RUNNING が途切れることはない
- 一方, I/O-intensive なタスクは細切れで実行された
- 両者が同時に実行されることでスケジューリングに不具合が発生することはなく, 想定通りの挙動となった

## Progress
### 7/27
- 最終的な出力イメージ(log_goal.txt)を作成
- xv6 をマルチコアで boot するようオプションを変更
+ Makefile の QEMUOPTS に sockets=$(CPUS),cores=1,threads=1 を追加

### 7/28
- bufread システムコール追加の練習として, "Hello, World!" を出力する hello コマンドを追加
+ 同じコマンドだが, ls はユーザプログラムで, open はシステムコールといった違いがある
- bufwrite/read システムコールの原型を作成
+ bufwrite : proc.h の buf 変数の値を変更する
+ bufread  : proc.h の buf 変数の値を print する

### 8/2
- clock の出力を追加
+ rdtsc は返り値uint64_t でそのまま使えないので, 32 bit ずつに分割する
+ 32 bit ずつを10進数のフォーマットで print してしまうと値が狂うので, 16進数に変換して print する
+ クロックの下位 5bit が 0 になった (QEMU の仕様が原因？)
- scheduling event の出力を追加

### 8/25
- scheduling event を追加
- cpu_from, cpu_to を cpu のみに
- prev_pstate の出力をサポート

### 9/1
- プロセスの状態が不連続になる不具合を修正
+ fork() 内の writelog() の表記が原因だった
- ログを読みやすくするための, 文字列置換スクリプトを追加
- 16進数変換のプログラムにおいて, 危ないと指摘を受けた部分を修正
- クロックの下位 5bit が 0 になる原因を調査
+ QEMU 外でも似たような挙動を確認した
- bufwrite の処理が軽すぎてログを取りきれない不具合を修正
+ cprintf() で時間を稼いだが, 本当は sleep(1) のようなものを入れたい

### 9/4
- bufwrite.c 内でのログ記録機能をサポート
+ ログ記録は1度きりに限定した
- bufwrite.c で forktest() を実行し, グラフを作りながら様子を観察した

### 9/5
- コアの稼働状態をグラフで図示するスクリプトを追加
+ 線の繋ぎ方に問題が残るものの, 7割程度は完成
- bufwrite コール内で計算に時間を要する CPU-intensive な命令を実装する予定

### 9/6
- volatile int の加算によって CPU-intensive な命令を実現
- 計測用スクリプトの様々な不具合を修正
+ 0 秒時に謎のデータが出現
+ 線が繋がらない / 意図していない繋がりかたをする不具合を確認

### 9/7
- file に write() を行う, I/O intensive な命令についてコア状態を測定
+ usertest 用のスクリプトがすでに用意されていたので, それを利用
- 計測用スクリプトの不具合を修正
+ sleep から復帰後に同一コアでプロセスが RUNNING になると, 線の繋がりがおかしくなる

### 9/8
- scheduler gaming の方針を決めるために, xv6 のソースコードをもう一度読み直す
+ scheduler を作り直す可能性あり

### 9/9
- スクリプトの描画において発見した不具合を修正
- xv6 スケジューラを, 優先度付き Round-Robin に改良
+ 改良前と改良後で動作に違いが出るか確かめているが, うまく visualize できていない
+ もしかしたら plot.py にまだ修正すべき点があるかもしれない

### 9/12
- MLFQ-like scheduler が, 概ね想定通りの動作をすることを確認
+ CPU-intensive なプロセスAが動いていたとき, 途中から CPU-intensive なプロセスBが動きはじめると, プロセスA は starvation-like な動きをすることが確認できた
- Priority Boost を実装
+ 上に挙げた MLFQ-like scheduler の症状をほどほど改善できた

### 9/13
- Priority Boost を修正
+ 「time slice を 10 回使い切ったときに Boost」 → 「timer interrupt が 10 回起こった時に Boost」 に変更

### 9/14
- 「微小時間だけ計算 → yield」を繰り返すワークロードを作成し, デフォルトでは ptable のグローバルロックによるスケジューラの遅延が発生することを確認
- global lock を最小限にとどめ, 各コアが独立して動くようにランキュー・スケジューラを実装

### 9/15
- 昨日の実装（デバッグ）の続き
+ 原因不明のパニック続きで難航

### 9/19
- Multiple runqueue scheduler の原型を実装
+ initproc の時、次 switch するプロセスの選び方がよく分からない
- runqueue の動作を print して可視化
+ マルチコアで動かすと挙動がおかしくなってしまう

### 9/20
- Multiple runqueue scheduler が動作するようにデバッグ
+ switch 後に myproc() がNULLになる問題を解決
- QEMU のアクセラレータとして, KVM を用いるように Make のオプションを変更
+ Clock の下位 5 bit が 0 になる問題は解消された
+ しかし, 実行時間（経過clock）が 30 倍程度長くなった
- Global lock をスケジューラから無くすよう再実装
+ 前段階として, スケジューラでは ptable ではなく, runqueuetable の Global lock を使用するように改変していく

### 9/21
- 想定通りに cpu 時間が割り当てられているかどうかを確認
+ 概ねスケジューラで意図している通りの動作をしていることを確認
+ fairness の観点で改善の余地あり
- multiple runqueue scheduler で測定したデータを使用すると, plot 用のプログラムがおかしくなる不具合を修正
- Global lock をスケジューラから無くし, ひたすらデバッグ
+ acquire が 2 回呼ばれている箇所を探す

### 9/22
- マルチコア・シングルコアで global lock を取らずに multiple runqueue scheduler が動作するようになった
+ lock の acquire が重複して呼ばれるバグに悩まされていたが, 自分が変更した ptable.lock の問題ではなく,
デバッグ用の cprintf 呼出中に interrupt が入ることで, console の lock が acquire されてしまっていたことが原因であった
+ コア数を増やした場合, global lock の影響で Round-robin scheduler は性能低下が起きたが, multiple runqueue scheduler では性能を維持することを確認した
+ プロセス数を 10000 個に増やした時にスケジューラの速度に大きな差が見られた (Round-robin scheduler では ptable の全探索を行うため)

### 9/25
- CPU 使用率を測定するスクリプトを追加
- マルチコアにおける Visualize 時に, RUNNING でない大きな空洞が生じるバグを修正. 期待通りの結果を取得できた
+ YIELD イベント時のロギングが一部機能していないことが原因だった
+ CPU 使用率を平均 10% から 45% に高めることができた

### 9/27
- 同じプロセスが特定のコアで動き続けないように, scheduler を修正
+ work stealing がうまく機能していない...?
- ワークロードの終了時間の出力機能を追加
- 全てのプロセスの fork が完了するまでロギングを開始しない仕様に変更

### 9/28
- work stealing のデバッグ(大苦戦)
- シングルコアの場合に, bufwrite 時に同じ pid のプロセスを push してしまうバグを修正
+ yield 時に ptable のlock を獲得する位置が浅いことが原因だった
- 2 コアの場合, 起動すらしなくなってしまった...

### 9/29
- work stealing のデバッグ(大苦戦)

### 10/02
- work stealing のデバッグ
+ 頻繁に panic が起きるものの, 起動するようにはなった
+ scheduler でランキューが空でないと判定された後に, 自身のランキューが持つプロセスが steal されて, 空となった runqueue から dequeue しようとしていることが原因だった
+ 欲を出して panic する前に cprintf を挟むと, cprintf 内で trap が発生しバグ原因が特定できなくなってしまうのでやめた方が良い[教訓]
- lock 状態の visualize
+ とりあえず ptable の lock のみ実装

### 10/03
- 安定性の改善
+ bufwrite 実行時, 高確率でクラッシュするバグを修正
+ push_rq, pop_rq 内で ptable のロックをとっていないことが原因だった
- runqueue データ構造の再実装
+ linked list から ring buffer に変更しようとしたが, bufwrite() 時のバグが治らず一時断念
+ ptable の lock をとってしまうと, 性能が落ちてしまうため
- データ記録開始タイミングを, 全てのプロセスの fork が終了したタイミングに変更

### 10/05
- グラフに生じる謎の空白の原因調査
+ [原因1] lock が適切に取れていないことで, steal された プロセスが描画されていなかった
+ [原因2] 描画したグラフを保存する際の問題
+ データ数が多すぎると保存する際に抜け落ちが発生する(?)
+ ひとまずはスクリーンショットで対応する
- 安定性の改善
+ push_rq_arg を ptable のロックを確保した上で呼び出すように修正
+ bufwrite 時の exit 忘れを修正
+ クラッシュは減ったが, デッドロックは改善されていない

### 10/06
- response time, turn around time の出力をサポート
+ fork 直後, running 時, exit 時に記録した clock から計算する
- fork が全て終了してから各プロセスが計算を実行するように修正
+ fork が全て終了するまで wait するシステムコール waitfork() を追加
- 空白が再び生じてしまっている

### 10/10
- プロセス数の上限を増やした際の response time, turn around time の影響を測定
+ NPROC 100 → 10000 のとき, response time は 両者ともに x1.5, turn around time は round robin の方が長くなった
+ NPROC = 10000 程度では実行時間に大きな影響が見られないため, さらに値を大きくしたい
+ が, 起動しなくなってしまう (メモリ領域が足りなくなるため？)
- work stealing 時のロギングがまだ完全に出来ていない...
+ push_rq() 使ってないのに, コードから削除すると動かなくなるのはなぜ？

### 10/11
- 論文のタイトルと概要の暫定版を作成
- xv6 の起動を virsh に変更
+ 使い方が難しくてよく分からない
- cpu 時間割り当ての割合をグラフとして出力するスクリプトを追加
+ round robin, multiple runqueue ともに概ね fair であることを確認した
