# TODO

* Migrate from [`SecKeychainItemExport`][1] to [`SecItemExport`][2]
* Migrate from [`SecKeychainSearchCreateFromAttributes`][3] to [`SecItemCopyMatching`][4]
* Work out whether response is correct for possibly duplicate entries: `(-25293) The user name or passphrase you entered is not correct.`

[1]: https://developer.apple.com/documentation/security/1412386-seckeychainitemexport
[2]: https://developer.apple.com/documentation/security/1394828-secitemexport?language=objc
[3]: https://developer.apple.com/documentation/security/1515366-seckeychainsearchcreatefromattri
[4]: https://developer.apple.com/documentation/security/1398306-secitemcopymatching?language=objc
