package main

import (
	"flag"
	"fmt"
	"syscall"
	"time"
)

func fetchAllUserProcess() []ProcessInfo {
	ps, _ := Pids()
	var infos []ProcessInfo
	for _, p := range ps {
		if p == int32(MyPID) {
			continue
		}
		info, err := FetchProcessInfo(p)
		if err != nil {
			fmt.Println("E:", err)
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

func init() {
	syscall.Mlockall(syscall.MCL_FUTURE)
}

func main() {
	var addr string
	var user, passwd string
	var dbname string

	flag.StringVar(&addr, "addr", "http://127.0.0.1:8086", "infulxdb address")
	flag.StringVar(&user, "user", "snyh", "influxdb user")
	flag.StringVar(&passwd, "password", "snyh", "influxdb password")
	flag.StringVar(&dbname, "dbname", "test", "influxdb database name")
	flag.Parse()

	client, err := NewClient(addr, user, passwd, dbname)
	if err != nil {
		fmt.Println("E:", err)
		return
	}
	defer client.Close()

	fmt.Println("Start pushing..", time.Now())
	for {
		time.Sleep(time.Millisecond * 500)
		err = client.Push(fetchAllUserProcess())
		if err != nil {
			fmt.Println("E2:", err)
		}
	}
}
