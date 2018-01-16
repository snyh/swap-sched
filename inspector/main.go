package main

import (
	"flag"
	"fmt"
	"os"
	"time"
)

func Error(fmtStr string, args ...interface{}) {
	fmt.Fprintf(os.Stderr, fmtStr, args...)
}

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
		client, err = NewInfluxClient(addr, user, passwd, dbname)
		if err != nil {
			Error("E: %v\n", err)
			return
		}
		defer client.Close()
	}

	for {
		time.Sleep(time.Millisecond * 500)
		//PushProcessInfo(client)
		err = PushMemInfos(client)
		if err != nil {
			Error("E2: %v\n", err)
		}
	}
}
