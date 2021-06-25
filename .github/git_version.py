import subprocess
import datetime

dt_date = datetime.datetime.now()
revision = (
    subprocess.check_output(["git", "describe", "--abbrev=7", "--dirty", "--always", "--tags"])
    .strip()
    .decode("utf-8")
)
print("-DGIT_HASH='\"Version: %s built: %s\"'" % (revision, dt_date.strftime("%d %b %Y %H:%M")))