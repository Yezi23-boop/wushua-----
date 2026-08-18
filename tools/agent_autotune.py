import sys


MESSAGE = (
    "当前仓库未包含 project/speed_loop_autotune，旧版 agent 自动调参入口已不可用。"
    "若后续恢复该专题目录，需要同步恢复 host 脚本与测试。"
)


def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]

    if len(argv) > 0 and argv[0] in ("-h", "--help"):
        print(MESSAGE)
        return 0

    print(MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
