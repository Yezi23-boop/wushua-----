import pathlib
import sys


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[1]
PROJECT_ROOT_TEXT = str(PROJECT_ROOT)
if PROJECT_ROOT_TEXT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT_TEXT)

from project.speed_loop_autotune.host.agent_autotune import *  # noqa: F401,F403


if __name__ == "__main__":
    sys.exit(main())
