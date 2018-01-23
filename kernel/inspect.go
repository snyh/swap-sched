package main

import (
	"fmt"
	"io/ioutil"
	"os"
	"strconv"
	"strings"
)

type Range struct {
	Name    string
	Start   uint64
	End     uint64
	Perm    string
	Address uint64
}

func (r Range) String() string {
	return fmt.Sprintf("%s 0x%x@0x%x~0x%x(%s) %dKB",
		r.Name, r.Address, r.Start, r.End,
		r.Perm, (r.End-r.Start)/1024)
}

func ParseMapsLine(d string) (Range, error) {
	fs := strings.Fields(d)
	if len(fs) < 5 {
		return Range{}, fmt.Errorf("Invalid input %q\n", d)
	}
	name := fs[len(fs)-1]
	if len(name) < 2 {
		name = "[anon]"
	}
	rs := strings.Split(fs[0], "-")
	begin, err := strconv.ParseUint(rs[0], 16, 64)
	if err != nil {
		return Range{}, err
	}
	end, err := strconv.ParseUint(rs[1], 16, 64)
	if err != nil {
		return Range{}, err
	}

	return Range{
		Name:  name,
		Start: begin,
		End:   end,
		Perm:  fs[1],
	}, nil
}

func ParseMaps(fpath string) ([]Range, error) {
	var ret []Range
	maps, err := ioutil.ReadFile(fpath)
	for _, line := range strings.Split(string(maps), "\n") {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		r, err := ParseMapsLine(line)
		if err != nil {
			return nil, err
		}
		ret = append(ret, r)
	}
	return ret, err
}

func ParseRefault(d string) (int, map[uint64]int, error) {
	fs := strings.Fields(d)
	if len(fs) < 2 {
		return 0, nil, fmt.Errorf("Invalid Input")
	}

	pid, err := strconv.ParseUint(fs[1], 0, 32)
	if err != nil {
		return 0, nil, err
	}

	ret := make(map[uint64]int)

	for i := 2; i+2 <= len(fs); i += 2 {
		if strings.HasPrefix(fs[i], "0x") {
			addr, err := strconv.ParseUint(fs[i], 0, 64)
			if err != nil {
				return 0, nil, err
			}
			count, err := strconv.ParseUint(fs[i+1], 0, 32)
			if err != nil {
				return 0, nil, err
			}
			ret[addr] = int(count)
		}
	}
	return int(pid), ret, nil
}

func Parse(d string) (int, map[Range]int, error) {
	pid, counts, err := ParseRefault(d)
	if err != nil {
		return 0, nil, err
	}
	if len(counts) == 0 {
		return 0, nil, fmt.Errorf("%q zero counts", d)
	}
	rs, err := ParseMaps(fmt.Sprintf("/proc/%d/maps", pid))
	if err != nil {
		return 0, nil, err
	}
	ret := make(map[Range]int)
	for addr, c := range counts {
		for _, r := range rs {
			if r.Start <= addr && r.End > addr {
				r.Address = addr
				ret[r] = c
			}
		}
	}
	return pid, ret, nil
}

func ParseAndDump(d string) error {
	pid, ret, err := Parse(d)
	if err != nil {
		return err
	}
	for r, c := range ret {
		fmt.Printf("%d %v %v\n", pid, c, r)
	}
	return nil
}

func main() {
	bs, err := ioutil.ReadAll(os.Stdin)
	if err != nil {
		fmt.Println("E:", err)
		return
	}
	for _, line := range strings.Split(string(bs), "\n") {
		if line == "" || !strings.Contains(line, "reduce") || !strings.Contains(line, "*") {
			continue
		}
		err = ParseAndDump(line)
		if err != nil {
			fmt.Printf("%q --> %v\n", line, err)
		}
	}
}
