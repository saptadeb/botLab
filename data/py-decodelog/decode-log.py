import sys
import lcm

# put lcm types you need here
# run make once to generate python lcm types
from lcmtypes import pose_xyt_t

if len(sys.argv) < 2:
    sys.stderr.write("usage: decode-log <logfile>\n")
    sys.exit(1)

log = lcm.EventLog(sys.argv[1], "r")

for event in log:
    if event.channel == "TRUE_POSE":
        msg = pose_xyt_t.decode(event.data)
        print("TruePose:")
        print("timestamp= %d" % msg.utime)
        print("pose: (%d, %d, %d)" % (msg.x, msg.y, msg.theta))