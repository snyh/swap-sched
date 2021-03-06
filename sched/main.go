package main

import (
	"flag"
	"fmt"
	"pkg.deepin.io/lib/dbus"
	"syscall"
	"time"
)

var SystemCGroupRoot string

func main() {
	var cfg = TuneConfig{
		RootCGroup:            "77@dde/uiapps",
		FreezeInactiveAppTime: time.Second * 1,
	}
	var daemon bool
	flag.BoolVar(&daemon, "daemon", false, "run as a daemon")
	flag.StringVar(&SystemCGroupRoot, "cgroup-root", "/sys/fs/cgroup", "root path of cgroup virtual file system")
	flag.BoolVar(&cfg.MemoryLock, "lock", false, "lock daemon memory")
	flag.Parse()

	err := CheckPrepared(cfg.RootCGroup)
	if err != nil {
		fmt.Println(err)
		return
	}

	if daemon {
		err = RunAsDaemon(cfg)
	} else {
		err = RunAsWrapper(flag.Arg(0))
	}
	if err != nil {
		fmt.Println(err)
	}
}

func RunAsDaemon(cfg TuneConfig) error {
	d := NewDispatcher(cfg)
	err := dbus.InstallOnSession(&DBusWrapper{d, false})
	if err != nil {
		return err
	}
	go ActiveWindowHandler(d.ActiveWindowHanlder).Monitor("")
	if cfg.MemoryLock {
		syscall.Mlockall(syscall.MCL_CURRENT)
	}
	d.Blance()
	return nil
}

func RunAsWrapper(app string) error {
	if app == "" {
		return fmt.Errorf("指定需要包装的命令行程序")
	}

	bus, err := dbus.SessionBus()
	if err != nil {
		return err
	}
	daemon := bus.Object("com.deepin.SwapSched", "/com/deepin/SwapSched")
	if _, err = daemon.GetProperty("com.deepin.SwapSched.Ping"); err != nil {
		return fmt.Errorf("请先执行 sched -daemon. E:%v", err)
	}
	return daemon.Call("com.deepin.SwapSched.Run", dbus.FlagNoAutoStart, app).Err
}

type DBusWrapper struct {
	core *Dispatcher
	Ping bool
}

func (d *DBusWrapper) GetDBusInfo() dbus.DBusInfo {
	return dbus.DBusInfo{
		"com.deepin.SwapSched",
		"/com/deepin/SwapSched",
		"com.deepin.SwapSched",
	}
}
func (d *DBusWrapper) Run(desktopId string) error {
	var err error
	go func() {
		err = d.core.Run(desktopId)
	}()
	time.Sleep(time.Millisecond * 200)
	if err != nil {
		return fmt.Errorf("Can't Launch %q, E: %v", desktopId, err)
	}
	return nil
}
