package main

import (
	"encoding/json"
	"fmt"
	"github.com/influxdata/influxdb/client/v2"
	"io"
	"time"
)

type InfluxClient struct {
	c      client.Client
	dbname string
}
type DumpClient struct {
	w io.Writer
}

func (c DumpClient) Push(infos []ProcessInfo) error {
	return json.NewEncoder(c.w).Encode(infos)
}
func (c DumpClient) Close() error { return nil }

type DataSource interface {
	Push([]ProcessInfo) error
	Close() error
}

func (c *InfluxClient) Push(infos []ProcessInfo) error {
	bp, err := client.NewBatchPoints(client.BatchPointsConfig{
		Database:  c.dbname,
		Precision: "ms",
	})
	if err != nil {
		return err
	}

	for _, info := range infos {
		tags := map[string]string{
			"pid":  fmt.Sprintf("%d", info["Pid"]),
			"name": info["Name"].(string),
		}
		pt, err := client.NewPoint("process", tags, info, time.Now())
		if err != nil {
			return fmt.Errorf("NewPoint: %v", err)
		}
		bp.AddPoint(pt)
	}
	return c.c.Write(bp)
}

func (c *InfluxClient) Close() error { return c.c.Close() }

func NewInfluxClient(addr string, user string, passwd string, dbname string) (*InfluxClient, error) {
	c, err := client.NewHTTPClient(client.HTTPConfig{
		Addr:     addr,
		Username: user,
		Password: passwd,
	})
	if err != nil {
		return nil, err
	}
	_, _, err = c.Ping(time.Second)
	if err != nil {
		return nil, err
	}
	c.Query(client.Query{
		Command: fmt.Sprintf("drop database %s", dbname),
	})
	c.Query(client.Query{
		Command: fmt.Sprintf("create database %s", dbname),
	})
	return &InfluxClient{c, dbname}, nil
}
