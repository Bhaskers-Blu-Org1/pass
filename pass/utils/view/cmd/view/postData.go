// author john.d.sheehan@ie.ibm.com

package main

import (
	"encoding/json"
	"io"
	"io/ioutil"
	"log"
)

type postData struct {
	Name    string    `json:"name"`
	Type    string    `json:"type"`
	Sensor  int       `json:"sensor"`
	Channel int       `json:"channel"`
	Values  []float32 `json:"values"`
}

// DataTypes valid types of POST
type DataTypes struct {
	types []string
}

// NewDataTypes new instance of DataTypes
func NewDataTypes(types []string) *DataTypes {
	return &DataTypes{
		types: types,
	}
}

// IsValidType check string is of known type
func (d DataTypes) IsValidType(t string) bool {
	found := false
	for _, types := range d.types {
		if t == types {
			return true
		}
	}

	return found
}

// IsValidDataType ensure POST is of valid type
func (d DataTypes) IsValidDataType(data io.ReadCloser) ([]byte, bool) {
	body, err := ioutil.ReadAll(data)
	if err != nil {
		log.Print(err)
		return nil, false
	}

	var msg postData
	err = json.Unmarshal(body, &msg)
	if err != nil {
		log.Print(err)
		return nil, false
	}

	found := false
	for _, t := range d.types {
		if t == msg.Type {
			found = true
			break
		}
	}

	if !found {
		log.Printf("unknown type: %s", msg.Type)
		return nil, false
	}

	return body, true
}
