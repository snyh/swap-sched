package main

import (
	"fmt"
	"sync"
	"time"
)

type Dispatcher struct {
	sync.Mutex

	counter int

	activeApp    *UIApp
	inactiveApps []*UIApp
}

func NewDispatcher() *Dispatcher {
	return &Dispatcher{
		counter: 0,
	}
}

func (d *Dispatcher) Counter() int {
	d.Lock()
	d.counter = d.counter + 1
	d.Unlock()
	return d.counter
}

func (d *Dispatcher) Run(cmd string) error {
	app, err := NewApp(d.Counter(), cmd)
	if err != nil {
		return err
	}
	return app.Run()
}

func (d *Dispatcher) ActiveWindowHanlder(pid int) {
	var newActive *UIApp
	d.Lock()
	defer func() {
		d.Unlock()
		d.setActiveApp(newActive)
	}()

	if d.activeApp != nil && d.activeApp.HasChild(pid) {
		return
	}
	for _, app := range d.inactiveApps {
		if app.HasChild(pid) {
			newActive = app
			break
		}
	}
}

func (d *Dispatcher) setActiveApp(newActive *UIApp) {
	if d.activeApp == newActive {
		return
	}

	d.Lock()
	var newInactiveApps []*UIApp
	if d.activeApp != nil {
		newInactiveApps = append(newInactiveApps, d.activeApp)
	}
	for _, i := range d.inactiveApps {
		if i == newActive {
			continue
		}
		newInactiveApps = append(newInactiveApps, i)
	}

	d.inactiveApps = newInactiveApps
	d.activeApp = newActive
	d.Unlock()

	d.blance()
}

func (d *Dispatcher) blance() {
	d.Lock()
	defer d.Unlock()

	if d.activeApp == nil {
		fmt.Println("Current Hasn't any registered APP")
	} else {
		fmt.Println("Current Active APP is ", d.activeApp)
	}
}

func (d *Dispatcher) Blance() {
	for {
		time.Sleep(time.Second)
		d.blance()
	}
}
