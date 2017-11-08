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
	go d.Run("eog")
	err := d.Run("gedit")
	if err != nil {
		fmt.Println("TEST E:", err)
	}

}
