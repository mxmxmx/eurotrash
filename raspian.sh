UPDATE_URL="https://raw.githubusercontent.com/mxmxmx/eurotrash/master/"
wget -q ${UPDATE_URL}/installers/common.sh -O /tmp/raspapcommon.sh
source /tmp/raspapcommon.sh && rm -f /tmp/raspapcommon.sh

function update_system_packages() {
    install_log "Updating sources"
    sudo apt-get update -y --force-yes || install_error "Unable to update package list"
}

function install_dependencies() {
    install_log "Installing required packages"
    sudo apt-get install -y --force-yes lighttpd $php_package git hostapd dnsmasq || install_error "Unable to install dependencies"
}

install_raspap
