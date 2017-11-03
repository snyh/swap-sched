package main

import (
	"flag"
	"fmt"
	"os"
	"syscall"
	"time"
)

func Error(fmtStr string, args ...interface{}) {
	fmt.Fprintf(os.Stderr, fmtStr, args...)
}

func fetchAllUserProcess() []ProcessInfo {
	ps, err := Pids()
	if err != nil {
		Error("Panic: %v\n", err)
		return nil
	}
	var infos []ProcessInfo
	for _, p := range ps {
		if p == int32(MyPID) {
			continue
		}
		info, err := FetchProcessInfo(p)
		if err != nil {
			Error("E: %v\n", err)
			// The process has been exists
			continue
		}
		if info["VMS"].(int64) == 0 {
			// It's a zombie or kernel process
			continue
		}
		infos = append(infos, info)
	}
	return infos
}

var MyPID = syscall.Getpid()

func main() {
	var addr string
	var user, passwd string
	var dbname string
	var dump bool

	flag.StringVar(&addr, "addr", "http://127.0.0.1:8086", "infulxdb address")
	flag.StringVar(&user, "user", "snyh", "influxdb user")
	flag.StringVar(&passwd, "password", "snyh", "influxdb password")
	flag.StringVar(&dbname, "dbname", "test", "influxdb database name")
	flag.BoolVar(&dump, "dump", false, "only dump the stat data")
	flag.Parse()

	var client DataSource
	var err error

	Error("Start pushing..%v\n", time.Now())

	if dump {
		client = DumpClient{os.Stdout}
	} else {
		syscall.Mlockall(syscall.MCL_FUTURE)

		client, err = NewInfluxClient(addr, user, passwd, dbname)
		if err != nil {
			Error("E: %v\n", err)
			return
		}
		defer client.Close()
	}

	for {
		time.Sleep(time.Millisecond * 500)
		err = client.Push(fetchAllUserProcess())
		if err != nil {
			Error("E2: %v\n", err)
		}
	}
}
