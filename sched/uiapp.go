package main

import (
	"fmt"
	"os"
	"os/exec"
	"path"
)

const baseCGDir = "/sys/fs/cgroup/memory/77@dde/uiapps"

type UIApp struct {
	cgroup string
	CMD    string
	limit  int
}

func (app *UIApp) HasChild(pid int) bool {
	panic("not implement")
}
func (app *UIApp) RSS() int {
	panic("not implement")
}
func (app *UIApp) SetLimitRSS(v int) error {
	app.limit = v

	// TODO: send SIGSTOP to process in cgroup.procs for avoiding write failed
	f, err := os.Open(path.Join(app.cgroup, "limit_in_bytes"))
	if err != nil {
		return err
	}
	defer f.Close()
	_, err = fmt.Fprintf(f, "%v", app.limit)
	return err
}

func (app *UIApp) LimitRSS() int {

	return app.limit
}

func (app *UIApp) Run() error {
	defer os.RemoveAll(app.cgroup)
	return exec.Command("cgexec", app.CMD).Run()
}

func NewApp(id int, cmd string) (*UIApp, error) {
	cgroup := fmt.Sprintf("%s/%d", baseCGDir, id)
	var err error
	err = os.Mkdir(cgroup, 0700)
	if err != nil {
		return nil, err
	}
	return &UIApp{cgroup, cmd, -1}, nil
}
