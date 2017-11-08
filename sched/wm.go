package main

import (
	"github.com/BurntSushi/xgb/xproto"
	"github.com/BurntSushi/xgbutil"
	"github.com/BurntSushi/xgbutil/ewmh"
	"github.com/BurntSushi/xgbutil/xevent"
	"github.com/BurntSushi/xgbutil/xprop"
	"github.com/BurntSushi/xgbutil/xwindow"
)

type ActiveWindowHandler func(int)

func (cb ActiveWindowHandler) Monitor(display string) error {
	xu, err := xgbutil.NewConnDisplay(display)
	if err != nil {
		return err
	}
	atmNAW, err := xprop.Atm(xu, "_NET_ACTIVE_WINDOW")
	if err != nil {
		return err
	}
	root := xu.RootWin()
	xwindow.New(xu, root).Listen(xproto.EventMaskPropertyChange)

	xevent.PropertyNotifyFun(
		func(X *xgbutil.XUtil, e xevent.PropertyNotifyEvent) {
			if e.Atom != atmNAW {
				return
			}
			aid, err := ewmh.ActiveWindowGet(xu)
			if err != nil {
				return
			}
			pid, err := xprop.PropValNum(xprop.GetProperty(xu, aid, "_NET_WM_PID"))
			if err != nil {
				return
			}
			if pid != 0 && cb != nil {
				cb(int(pid))
			}
		}).Connect(xu, root)
	xevent.Main(xu)
	return nil
}
