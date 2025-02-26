import sys

default_name="cleaned_code_"

state = "all"
file = open("vmcache.cpp", 'r')
output_linux = open(default_name+"linux.cpp", "w")
output_osv = open(default_name+"osv.cpp", "w")
for line in file:
    stripped = line.strip()
    if stripped.startswith('#ifdef LINUX'):
        if state == "all":
            state = "linux"
            continue
        else:
            print("error when parsing. ifdef LINUX at the wrong place.\n offending line: " + stripped)
            file.close()
            output_osv.close()
            output_linux.close()
            sys.exit(0)
    if stripped.startswith("#ifdef OSV"):
        if state == "all":
            state = "osv"
            continue
        else:
            print("error when parsing. ifdef OSV at the wrong place.\n offending line: " + stripped)
            file.close()
            output_osv.close()
            output_linux.close()
            sys.exit(0)
    if stripped.startswith("#endif"):
        if state == "linux" or state == "osv":
            state = "all"
            continue
        else:
            print("error when parsing. endif at the wrong place.\n offending line: " + stripped)
            file.close()
            output_osv.close()
            output_linux.close()
            sys.exit(0)

    if state == "all":
        output_osv.write(line)
        output_linux.write(line)
    elif state == "linux":
        output_linux.write(line)
    elif state == "osv":
        output_osv.write(line)

file.close()
output_osv.close()
output_linux.close()
