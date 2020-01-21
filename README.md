Sample code in `C` to calculate octave bands and frequency bins for an incoming data stream of shorts.

This provides,
- code to emit certain frequencies, or linear chirps over tcp
- code to process data from tcp, into frequency bins, octave bands, or wav files
- code to view the octave bands, or frequency bins on a webpage

## Quickstart
In terminal 1
```
vagrant up
vagrant ssh
cd /vagrant/pass
rm -rf lib
mkdir -p lib
make
cd utils
./emit_freq
# by default, this will emit a signal with amplitude 0.01, frequency alternating between 256Hz and 2048Hz with a sample rate of 500000 to tcp port 1234
```

In terminal 2
```
vagrant ssh
cd /vagrant/pass/utils
./multi_octave_bands 127.0.0.1 1234
# by default, this will expect a signal with sample rate 500000 on port 1234. Results are POSTed to http://localhost:5100/data
```

In terminal 3
```
vagrant ssh
cd /vagrant/pass/utils
./multi_frequency_bins 127.0.0.1 1234
# by default, this will expect a signal with sample rate 500000 on port 1234. Results are POSTed to http://localhost:5100/data
```

In terminal 4
```
vagrant ssh
cd /vagrant/pass/utils
./viewer -static /vagrant/pass/utils/view/cmd/view/static/
```

In browser
```
http://localhost:5100
```


`emit_freq` accepts the following options,

| flag | option          | default | comments                                                            |
| ---- | --------------- | -------:| -------------------------------------------------------------------:|
| -a   | amplitude       | 0.01    |                                                                     |
| -c   | channels        | 1       |                                                                     |
| -e   | endian swap     | 0 (no)  |                                                                     |
| -h   | include header  | 1 (yes) |                                                                     |
| -i   | max iteration   | 1200    |                                                                     |
| -l   | frequency lower | 256.0   |                                                                     |
| -m   | max amplitude   | 1.0     |                                                                     |
| -n   | port number     | 1234    |                                                                     |
| -p   | period          | 10      | (seconds between switching from lower frequency to upper frequency) |
| -r   | sample rate     | 500000  |                                                                     |
| -s   | sensors         | 1       |                                                                     |
| -u   | frequency upper | 2048    |                                                                     |
| -v   | vrebose         | 0 (no)  |                                                                     |


`multi_octave_bands` and `multi_frequency_bins` accept the following options,

| flag | option      | default                    | comments |
| ---- | ----------- | --------------------------:| -------- |
| -c   | channels    | 1                          |          |
| -e   | endian swap | 0 (no)                     |          |
| -h   | has header  | 1 (yes)                    |          |
| -o   | origin ip   | 127.0.0.1                  |          |
| -p   | port number | 1234                       |          |
| -r   | sample rate | 500000                     |          |
| -s   | sensors     | 1                          |          |
| -u   | url         | http://localhost:5100/data |          |
| -v   | verbose     | 0 (no)                     |          |


`viewer` accepts the following options

| flag     | option               | default | comments
| -------- | -------------------- | -------:| --------
| --http   | http port            | 5100    |
| --static | path to `index.html` |         | no default, mandatory parameter. `/vagrqnt/pass/utils/view/cmd/view/static` contains a usable `index.html`
