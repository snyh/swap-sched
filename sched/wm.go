package main

import (
	"fmt"
	"github.com/BurntSushi/xgb/xproto"
	"github.com/BurntSushi/xgbutil"
	"github.com/BurntSushi/xgbutil/xevent"
	"github.com/BurntSushi/xgbutil/xprop"
	"github.com/BurntSushi/xgbutil/xwindow"
)

type ActiveWindowHandler func(int, int)

func (cb ActiveWindowHandler) Monitor(display string) error {
	xu, err := xgbutil.NewConnDisplay(display)
	if err != nil {
		return err
	}
	root := xu.RootWin()
	xwindow.New(xu, root).Listen(xproto.EventMaskPropertyChange)

	xevent.PropertyNotifyFun(
		func(X *xgbutil.XUtil, e xevent.PropertyNotifyEvent) {
			xid, err := xprop.PropValWindow(xprop.GetProperty(xu, root, "_NET_ACTIVE_WINDOW"))
			if err != nil {
				return
			}
			if xid == 0 {
				//				cb(0)
			} else {
				pid, err := xprop.PropValNum(xprop.GetProperty(xu, xid, "_NET_WM_PID"))
				if err != nil {
					fmt.Printf("DEBUG Can't find PID for XWindow %d . E:%v\n", xid, err)
					return
				}
				if pid != 0 && cb != nil {
					cb(int(pid), int(xid))
				}
			}
		}).Connect(xu, root)
	xevent.Main(xu)
	return nil
}
