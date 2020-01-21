# author john.d.sheehan@ie.ibm.com

$machine_name = "ubuntu-1804-pass"


$provision_root = <<'SCRIPT_ROOT'
apt-get update
apt-get upgrade -y
apt-get install -y  build-essential  gdb  git  valgrind  libcurl4-gnutls-dev  libfftw3-dev  libjson-c-dev  python3-dev  python3-pip

/usr/bin/python3 -m pip install --upgrade pip

SCRIPT_ROOT


$provision_user = <<'SCRIPT_USER'

cd
mkdir -p local
wget --quiet https://redirector.gvt1.com/edgedl/go/go1.13.4.linux-amd64.tar.gz
tar -xf go1.13.4.linux-amd64.tar.gz
mv go ${HOME}/local/


cat << 'EOF' > $HOME/.bashrc
export GOROOT=${HOME}/local/go
export GOPATH=${HOME}/golang
export PATH=${GOROOT}/bin:${HOME}/bin:${HOME}/local/bin:${HOME}/.local/bin:${PATH}

export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:/vagrant/pass/lib

export PS1='\n\@ \w \e[1;32m $(__git_ps1 "(%s)") \e[m \n: \u@\h \j %; '

cd /vagrant
EOF

SCRIPT_USER


Vagrant.configure("2") do |config|
  config.vm.box = "ubuntu/bionic64"

  config.vm.provision :shell, inline: $provision_root
  config.vm.provision :shell, privileged: false, inline: $provision_user

  config.vm.provider "virtualbox" do |vb|
    vb.customize ["modifyvm", :id, "--name", $machine_name]
    vb.customize ["modifyvm", :id, "--cpus", "2"]
    vb.customize ["modifyvm", :id, "--cpuexecutioncap", "90"]
    vb.customize ["modifyvm", :id, "--memory", "2048"]
  end

  config.vm.hostname = $machine_name
  config.vm.network "forwarded_port", guest: 5100, host: 5100
end
