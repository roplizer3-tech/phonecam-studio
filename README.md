# PhoneCam Studio v2.0.0

**تحويل كاميرا الموبايل (الأندرويد، بما يشمل Android 15) إلى كاميرا ويب احترافية 60 FPS منخفضة التأخير على Windows 11/10 عبر USB أو Wi-Fi أو بلوتوث، مع ذكاء اصطناعي حقيقي (ONNX DirectML)، عزل الخلفيات، فلاتر الوجه AR، وحل مشكلة عكس الكاميرا في WhatsApp وTelegram وZoom وOBS.**

---

## الجديد في الإصدار 2.0.0

1. **بث فائق السلاسة (حتى 60 FPS وزمن استجابة منخفض):**
   - **Android 15 Low-Latency H.264:** تفعيل `Constrained Baseline` و`KEY_LOW_LATENCY=1` و`KEY_OPERATING_RATE` و`WIFI_MODE_FULL_LOW_LATENCY` مع إيقاف معالجة EIS/OIS المكلفة زمنياً وتطبيق طابور إطارات يحافظ على تسلسل GOP (`GopSafeFrameQueue`) لتجنب التقطيع والتشويه.
   - **Windows DXVA Hardware Decode:** فك تشفير H.264 على كارت الشاشة عبر Media Foundation DXVA (`MFT_MESSAGE_SET_D3D_MANAGER`) مع فصل قراءة الشبكة عن المعالجة عبر خيوط منفصلة، وحلقة `DO_NOT_WAIT` لعزل الذكاء الاصطناعي ومعاينة خفيفة `640x360`.
2. **فلاتر ذكاء اصطناعي وفلاتر حقيقية (ليست مجرد تعديل ألوان):**
   - **عزل وتغيير الخلفية (`Selfie Segmentation` عبر ONNX Runtime + DirectML GPU):** عزل ضبابي `DSLR Bokeh Blur`، استوديو حديث، أضواء بوكيه دافئة، خلفية سايبر نيون، أو اختيار صورة مخصصة من جهازك.
   - **فلاتر الوجه بالواقع المعزز (`YuNet` 5-Landmarks AI):** نظارات `Aviator` و`Round Glasses`، قناع `Cyber Visor`، هالة ذهبية، ونجوم متحركة مثبّتة على ملامح الوجه الحقيقية، بالإضافة لنحت وتنحيف الوجه وتكبير وتفتيح العيون وتبييض الأسنان والتتبع التلقائي للوجه (`Center Stage`).
   - **شيدرات فنية حقيقية على كارت الشاشة (`HLSL Pixel Shaders`):** أنمي/كرتون (`Cel-Shading + Sobel Edges`)، رسم بالقلم الرصاص، شريط VHS 1995، حواف نيون سايبر، و`Cinema Halation`.
3. **إصلاحات USB وWi-Fi وإضافة اتصال بلوتوث:**
   - **USB فوري:** حزمة `adb.exe` الرسمية من Google مدمجة داخل البرنامج (`tools/adb/`) مع كشف حالة تفويض الجهاز لحظياً.
   - **Wi-Fi موثوق:** قواعد جدار حماية Windows Firewall تضاف تلقائياً وقت التثبيت + بث استكشاف نشط (`PCAM_PROBE`) على المنفذ `54821`.
   - **Bluetooth Classic RFCOMM:** إمكانية البث عبر البلوتوث مع ضبط تلقائي للجودة (`640x360 @ 20 FPS`).
   - *(ميزة إضافية لأندرويد 14/15: إذا كان جهازك يدعم وضع USB Webcam الأصلي من النظام `Device as a Webcam` فسيظهر في إعدادات اتصال USB ككاميرا UVC قياسية بدون تطبيقات).*
4. **كاميرا نظام دائمة على Windows 11 (`System Virtual Camera`):**
   - أداة `PhoneCamVcamSetup.exe` تسجل `PhoneCam Studio` عبر `MFVirtualCameraLifetime_System` + `MFVirtualCameraAccess_AllUsers` وفئة `KSCATEGORY_VIDEO_CAMERA`، مع دعم `IMFSampleAllocatorControl` و`MF_DEVICEMFT_SENSORPROFILE_COLLECTION` لتظهر بشكل دائم حتى بعد إعادة التشغيل في Windows 11، تطبيقات المتجر، WhatsApp، Telegram، Zoom، Discord، وOBS (بدون الحاجة لتوقيع kernel `.sys` مدفوع، مع بقاء `DirectShow Filter` لدعم البرامج الكلاسيكية وWindows 10).

---

## التثبيت والتشغيل السريع (Quick Start)

1. نزّل الملف المجمع من صفحة **Releases** على GitHub:
   - **`PhoneCamStudio-v2.0.0-Android-Windows-Bundle.zip`** (يحتوي المثبّت لـ Windows 11/10 وتطبيق Android APK ودليل التشغيل السريع).
2. ثبّت **`PhoneCamStudio-Setup-x64.exe`** على ويندوز كـ **Administrator** (يتكفل تلقائياً بتسجيل الكاميرا الدائمة وفتح جدار الحماية وتجهيز ADB ونماذج الـ AI).
3. ثبّت **`PhoneCam-Android-release.apk`** على موبايل الأندرويد.
4. اختر طريقة الاتصال (**USB** أو **Wi-Fi** أو **Bluetooth**)، ثم اختر كاميرا **`PhoneCam Studio`** من داخل تطبيق المكالمات واستخدم تبويب **`فلاتر AI/AR`** وتبويب **`العكس`** (`Ctrl+Shift+M` أثناء المكالمة).
