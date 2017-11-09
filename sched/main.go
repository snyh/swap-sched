package main

import (
	"fmt"
)

func main() {
	if err := CheckPrepared(); err != nil {
		fmt.Println(err)
		return
	}

	d := NewDispatcher()
	go ActiveWindowHandler(d.ActiveWindowHanlder).Monitor("")
	go Test(d)
	d.Blance()
}

func Test(d *Dispatcher) {
	go d.Run("google-chrome-stable")
	go d.Run("deepin-terminal")
	go d.Run("wps")
	go d.Run("wpp")
	go d.Run("thunderbird")
	err := d.Run("gedit")
	if err != nil {
		fmt.Println("TEST E:", err)
	}
}
