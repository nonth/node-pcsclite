# node-pcsclite

[![npm](https://img.shields.io/npm/v/@nonth/pcsclite.svg)](https://www.npmjs.com/package/@nonth/pcsclite)
[![build status](https://img.shields.io/github/actions/workflow/status/nonth/node-pcsclite/ci.yml?logo=github)](https://github.com/nonth/node-pcsclite/actions/workflows/ci.yml)
[![node-pcsclite channel on discord](https://img.shields.io/badge/discord-join%20chat-61dafb.svg?logo=discord&logoColor=white)](https://discord.gg/bg3yazg)

Bindings over pcsclite to access Smart Cards. It works in **Linux**, **macOS** and **Windows**.

> 📌 **Looking for library to work easy with NFC tags?**  
> Then take a look at [nfc-pcsc](https://github.com/nonth/nfc-pcsc)
> which offers an easy to use high level API
> for detecting / reading and writing NFC tags and cards.


## Content

<!-- START doctoc generated TOC please keep comment here to allow auto update -->
<!-- DON'T EDIT THIS SECTION, INSTEAD RE-RUN doctoc TO UPDATE -->


- [Installation](#installation)
- [Example](#example)
- [Behavior on different OS](#behavior-on-different-os)
- [API](#api)
  - [Class: PCSCLite](#class-pcsclite)
    - [Event: `error`](#event-error)
    - [Event: `reader`](#event-reader)
    - [pcsclite.close()](#pcscliteclose)
    - [pcsclite.readers](#pcsclitereaders)
  - [Class: CardReader](#class-cardreader)
    - [Event: `error`](#event-error-1)
    - [Event: `end`](#event-end)
    - [Event: `status`](#event-status)
    - [reader.connect([options], callback)](#readerconnectoptions-callback)
    - [reader.disconnect(disposition, callback)](#readerdisconnectdisposition-callback)
    - [reader.transmit(input, res_len, protocol, callback)](#readertransmitinput-res_len-protocol-callback)
    - [reader.control(input, control_code, res_len, callback)](#readercontrolinput-control_code-res_len-callback)
    - [reader.close()](#readerclose)
- [FAQ](#faq)
  - [Can I use this library in my Electron app?](#can-i-use-this-library-in-my-electron-app)
  - [Are prebuilt binaries provided?](#are-prebuilt-binaries-provided)
  - [Disabling drivers to make pcsclite working on Linux](#disabling-drivers-to-make-pcsclite-working-on-linux)
  - [Which Node.js versions are supported?](#which-nodejs-versions-are-supported)
  - [Can I use this library in my React Native app?](#can-i-use-this-library-in-my-react-native-app)
- [Frequent errors](#frequent-errors)
  - [Error: Cannot find module '../build/Release/pcsclite.node'](#error-cannot-find-module-buildreleasepcsclitenode)
- [License](#license)

<!-- END doctoc generated TOC please keep comment here to allow auto update -->


## Installation

**Requirements:** **at least Node.js 18 or newer** (see [this FAQ](#which-nodejs-versions-are-supported) for more info)

1. **Node Native Modules build tools**

    Because this library uses Node Native Modules (C++ Addons),
    which are automatically built (using [node-gyp](https://github.com/nodejs/node-gyp))
    when installing via npm or yarn, you need to have installed **C/C++ compiler
    toolchain and some other tools** depending on your OS.
    
    **Please refer to the [node-gyp > Installation](https://github.com/nodejs/node-gyp#installation)**
    for the list of required tools depending on your OS and steps how to install them.

2. **PC/SC API in your OS**

    On **macOS** and **Windows** you **don't have to install** anything,
    **pcsclite API** is provided by the OS.
    
    On Linux/UNIX you'd probably need to install pcsclite library and daemon**.

    > For example, in Debian/Ubuntu:
    > ```bash
    > apt-get install libpcsclite1 libpcsclite-dev
    > ```
    > To run any code you will also need to have installed the pcsc daemon:
    > ```bash
    > apt-get install pcscd
    > ```

3. **Once you have all needed libraries, you can install node-pcsclite using npm:**

    ```bash
    npm install @nonth/pcsclite --save
    ```
    
    or using Yarn:
    
    ```bash
    yarn add @nonth/pcsclite
    ```


## Example

> 👉 **If you'd prefer an easy to use high level API** for detecting / reading and writing NFC tags and cards,
> take a look at [nfc-pcsc](https://github.com/nonth/nfc-pcsc).

```javascript
const pcsclite = require('@nonth/pcsclite');

const pcsc = pcsclite();

pcsc.on('reader', (reader) => {

    console.log('New reader detected', reader.name);

    reader.on('error', err => {
        console.log('Error(', reader.name, '):', err.message);
    });

    reader.on('status', (status) => {

        console.log('Status(', reader.name, '):', status);

        // check what has changed
        const changes = reader.state ^ status.state;

        if (!changes) {
            return;
        }

        if ((changes & reader.SCARD_STATE_EMPTY) && (status.state & reader.SCARD_STATE_EMPTY)) {

            console.log("card removed");

            reader.disconnect(reader.SCARD_LEAVE_CARD, err => {

                if (err) {
                    console.log(err);
                    return;
                }

                console.log('Disconnected');

            });

        }
        else if ((changes & reader.SCARD_STATE_PRESENT) && (status.state & reader.SCARD_STATE_PRESENT)) {

            console.log("card inserted");

            reader.connect({ share_mode: reader.SCARD_SHARE_SHARED }, (err, protocol) => {

                if (err) {
                    console.log(err);
                    return;
                }

                console.log('Protocol(', reader.name, '):', protocol);

                reader.transmit(Buffer.from([0x00, 0xB0, 0x00, 0x00, 0x20]), 40, protocol, (err, data) => {

                    if (err) {
                        console.log(err);
                        return;
                    }

                    console.log('Data received', data);
                    reader.close();
                    pcsc.close();

                });

            });

        }

    });

    reader.on('end', () => {
        console.log('Reader', reader.name, 'removed');
    });

});

pcsc.on('error', err => {
    console.log('PCSC error', err.message);
});
```


## Behavior on different OS

TODO document


## API

### Class: PCSCLite

The PCSCLite object is an EventEmitter that notifies the existence of Card Readers.

#### Event: `error`

* *err* `Error Object`. The error.

#### Event: `reader`

* *reader* `CardReader`. A CardReader object associated to the card reader detected

Emitted whenever a new card reader is detected.

#### pcsclite.close()

It frees the resources associated with this PCSCLite instance. At a low level it
calls [`SCardCancel`](https://pcsclite.apdu.fr/api/group__API.html#gaacbbc0c6d6c0cbbeb4f4debf6fbeeee6) so it stops watching for new readers.

#### pcsclite.readers

An object containing all detected readers by name. Updated as readers are attached and removed.

### Class: CardReader

The CardReader object is an EventEmitter that allows to manipulate a card reader.

#### Event: `error`

* *err* `Error Object`. The error.

#### Event: `end`

Emitted when the card reader has been removed.

#### Event: `status`

* *status* `Object`.
    * *state* The current status of the card reader as returned by [`SCardGetStatusChange`](https://pcsclite.apdu.fr/api/group__API.html#ga33247d5d1257d59e55647c3bb717db24)
    * *atr* ATR of the card inserted (if any)

Emitted whenever the status of the reader changes.

#### reader.connect([options], callback)

* *options* `Object` Optional
    * *share_mode* `Number` Shared mode. Defaults to `SCARD_SHARE_EXCLUSIVE`
    * *protocol* `Number` Preferred protocol. Defaults to `SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1`
* *callback* `Function` called when connection operation ends
    * *error* `Error`
    * *protocol* `Number` Established protocol to this connection.

Wrapper around [`SCardConnect`](https://pcsclite.apdu.fr/api/group__API.html#ga4e515829752e0a8dbc4d630696a8d6a5).
Establishes a connection to the reader.

#### reader.disconnect(disposition, callback)

* *disposition* `Number`. Reader function to execute. Defaults to `SCARD_UNPOWER_CARD`
* *callback* `Function` called when disconnection operation ends
    * *error* `Error`

Wrapper around [`SCardDisconnect`](https://pcsclite.apdu.fr/api/group__API.html#ga4be198045c73ec0deb79e66c0ca1738a).
Terminates a connection to the reader.

#### reader.transmit(input, res_len, protocol, callback)

* *input* `Buffer` input data to be transmitted
* *res_len* `Number`. Max. expected length of the response
* *protocol* `Number`. Protocol to be used in the transmission
* *callback* `Function` called when transmit operation ends
    * *error* `Error`
    * *output* `Buffer`

Wrapper around [`SCardTransmit`](https://pcsclite.apdu.fr/api/group__API.html#ga9a2d77242a271310269065e64633ab99).
Sends an APDU to the smart card contained in the reader connected to.

#### reader.control(input, control_code, res_len, callback)

* *input* `Buffer` input data to be transmitted
* *control_code* `Number`. Control code for the operation
* *res_len* `Number`. Max. expected length of the response
* *callback* `Function` called when control operation ends
    * *error* `Error`
    * *output* `Buffer`

Wrapper around [`SCardControl`](https://pcsclite.apdu.fr/api/group__API.html#gac3454d4657110fd7f753b2d3d8f4e32f).
Sends a command directly to the IFD Handler (reader driver) to be processed by the reader.

#### reader.close()

It frees the resources associated with this CardReader instance.
At a low level it calls [`SCardCancel`](https://pcsclite.apdu.fr/api/group__API.html#gaacbbc0c6d6c0cbbeb4f4debf6fbeeee6) so it stops watching for the reader status changes.


## FAQ

### Can I use this library in my [Electron](https://www.electronjs.org/) app?

**Yes, you can!** It works well.

But please read carefully [Using Native Node Modules](https://www.electronjs.org/docs/latest/tutorial/using-native-node-modules) guide in Electron documentation to fully understand the problematic.

**Note**, that because of Node Native Modules, you must build your app on target platform (you must run Windows build on Windows machine, etc.).
You can use CI/CD server to build your app for certain platforms.
For cross-platform builds, consider using [GitHub Actions](https://github.com/features/actions) which supports Windows, macOS, and Linux runners.

### Are prebuilt binaries provided?

No, because it brings more problems than it solves. The C++ code (Node Native Modules, C++ Addons) is built automatically during installation (using [node-gyp](https://github.com/nodejs/node-gyp)).

That means that cross-compilation is not possible by default. If you want to use this library in your [Electron](https://www.electronjs.org/) or [NW.js](https://nwjs.io/), see [Can I use this library in my Electron app?](#can-i-use-this-library-in-my-electron-app).

### Disabling drivers to make pcsclite working on Linux

In case there is another driver blocking the usb bus, you won't be able to access the NFC reader until you disable it. First, plug in your reader and check, which driver is being used:
```console
$ lsusb -t
/:  Bus 01.Port 1: Dev 1, Class=root_hub, Driver=xhci_hcd/12p, 480M
    |__ Port 3: Dev 6, If 0, Class=Chip/SmartCard, Driver=pn533, 12M
        ...
```
In my case, there is a `pn533` driver loaded by default. Now find the dependency tree of that driver:
```console
$ lsmod | grep pn533
Module                  Size  Used by
pn533_usb              20480  0
pn533                  45056  1 pn533_usb
nfc                   131072  1 pn533

```
We see, that there are drivers `nfc`, `pn533` and `pn533_usb` we need to disable. Create file in `/etc/modprobe.d/` with the following content:
```console
$ cat /etc/modprobe.d/nfc-blacklist.conf
blacklist pn533_usb
blacklist pn533
blacklist nfc
```
After reboot, there will be no driver blocking the usb bus anymore, so we can finally enable and start the pscs deamon:
```
# systemctl enable pcscd
# systemctl start pcscd
```

### Which Node.js versions are supported?

@nonth/pcsclite officially supports the following Node.js versions: **18.x, 20.x, 22.x** (LTS versions).

Older versions (8.x through 16.x) have reached end-of-life and are no longer supported.

### Can I use this library in my React Native app?

Short answer: **NO**

Explanation: **Mobile support is virtually impossible** because @nonth/pcsclite uses **Node Native Modules**
to access system **PC/SC API**. So the **Node.js runtime and PC/SC API** are required for @nonth/pcsclite to run.
That makes it possible to use it on the most of OS (Windows, macOS, Linux) **directly in Node.js**
or in **Electron.js and NW.js** desktop apps. On the other hand, these requirements are not normally met on mobile devices.
On top of that, React Native does not contain any Node.js runtime.


## Frequent errors

### Error: Cannot find module '../build/Release/pcsclite.node'

@nonth/pcsclite uses **Node Native Modules** (Node.js C++ Addon) to access PC/SC API (pcsclite).
The Node.js native C++ addon is built during installation via [node-gyp](https://github.com/nodejs/node-gyp)
(see package.json > scripts > [install](https://github.com/nonth/node-pcsclite/blob/master/package.json#L37)).
When you see the error `Cannot find module '../build/Release/pcsclite.node'`, something probably
**went wrong during the installation**.

Follow the steps below to resolve your problem:
1. If **there are any errors** in the output of the `npm install` resp. `yarn install`,
    * **ensure you meet all the requirements** described in the [Installation](#installation) section of this README.
        Then try reinstalling @nonth/pcsclite (npm uninstall / yarn remove and then npm install / yarn add).
    * **If the problem persists**, [open a new issue](https://github.com/nonth/node-pcsclite/issues/new)
        and be sure to include the output of the `npm install` resp. `yarn install`
        and the details about your platform, OS, Node.js version and npm/yarn version.
2. If **there are no errors** during the installation,
    * then try reinstalling @nonth/pcsclite (npm uninstall / yarn remove and then npm install / yarn add).
    * If it does not help, then examine the contents of the folder `node_modules/@nonth/pcsclite` in your project
        (in case you installed @nonth/pcsclite as a dependency). There should be a `build` folder with
        a `Release` folder inside. In the `Release` folder, there should be a `pcsclite.node` file.
        It is possible that this file is somewhere else. Whether you find the file somewhere or not,
        please [open a new issue](https://github.com/nonth/node-pcsclite/issues/new)
        and describe the problem and be sure to include the details
        about your platform, OS, Node.js version and npm/yarn version.


## License

[ISC](/LICENSE.md)
