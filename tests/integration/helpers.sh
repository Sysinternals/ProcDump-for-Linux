#
# Waits until the specified URL is reachable.
#
function waitforurl() {
  local url=$1

  i=0
  wget $url
  while  [ $? -ne 8 ]
  do
      ((i=i+1))
      if [[ "$i" -gt 10 ]]; then
          return -1
      fi
      sleep 2s
      wget $url
  done

  return 0
}

#
# Waits until the procdump child process has become available
#
function waitforprocdump() {
  i=0
  pid=$(ps -o pid= -C "procdump" | tr -d ' ')
  while [ ! $pid ]
  do
      ((i=i+1))
      if [[ "$i" -gt 10 ]]; then
          return -1
      fi
      sleep 1s
      pid=$(ps -o pid= -C "procdump" | tr -d ' ')
  done

  echo $pid
}

#
# Waits until the procdump status socket (in case of .net apps) is available
#
function waitforprocdumpsocket {
  local procdumpchildpid=$1
  local testchildpid=$2

  ps -A -l

  if [[ -v TMPDIR ]];
  then
      tmpfolder=$TMPDIR
  else
      tmpfolder="/tmp"
  fi
  prefixname="/procdump/procdump-status-"
  socketpath=$tmpfolder$prefixname$procdumpchildpid"-"$testchildpid

  echo "ProcDump .NET status socket: "$socketpath
  sudo ls /tmp/procdump

  i=0
  while [ ! -S $socketpath ]
  do
      ((i=i+1))
      if [[ "$i" -gt 20 ]]; then
        return -1
      fi
      sleep 1s
  done

  return 0
}