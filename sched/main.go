package main

import (
	"fmt"
)

func main() {
	d := NewDispatcher()
	go ActiveWindowHandler(d.ActiveWindowHanlder).Monitor("")
	go Test(d)
	d.Blance()
}

func Test(d *Dispatcher) {
	err := d.Run("gedit")
	if err != nil {
		fmt.Println("TEST E:", err)
	}
}
