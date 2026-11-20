# PhoneCam Studio

**تحويل كاميرا الموبايل (الأمامية والخلفية) إلى كاميرا ويب احترافية على Windows 11/10 مع فلاتر الألوان وتحسين المظهر وحل مشكلة عكس الكاميرا في WhatsApp وTelegram والمنصات الأخرى.**

---

## المميزات الأساسية (Key Features)

1. **كاميرا ويب افتراضية متوافقة على مستوى النظام:**
   - **Windows 11 (`MFCreateVirtualCamera`):** يظهر باسم `PhoneCam Studio` مباشرةً في **WhatsApp Desktop (UWP)**، **Telegram Desktop**، **Windows Camera**، **Zoom**، **Microsoft Teams**، **Discord**، **Google Meet (Chrome/Edge)**، و**OBS Studio** بدون الحاجة لشهادة Kernel Driver مدفوعة.
   - **Windows 10 / التوافق الشامل (`DirectShow Filter`):** يظهر باسم `PhoneCam Studio (DirectShow)` لجميع تطبيقات سطح المكتب والمتصفحات.
2. **دعم الكاميرا الأمامية والخلفية (+ العدسات المتعددة):**
   - تبديل فوري بين الكاميرا الأمامية والخلفية (أو العدسة الواسعة/المقربة) من الموبايل أو من تطبيق الويندوز.
   - تحكم عن بُعد في الفلاش (Torch)، الفوكس (Tap-to-Focus)، الزوم، الدقة (`480p / 720p / 1080p`)، ومعدل الإطارات (`30 / 60 FPS`).
3. **بث منخفض التأخير عبر Wi-Fi أو كابل USB:**
   - **Wi-Fi Auto-Discovery:** الموبايل يظهر تلقائياً في تطبيق الويندوز مع حماية برمز **PIN من 4 أرقام** (SHA-256 Challenge-Response).
   - **USB (ADB Forward / USB Tethering):** اتصال بضغطة زر عبر كابل USB لأقل تأخير ممكن (~15ms).
4. **معالجة الألوان وتحسين المظهر على كارت الشاشة (Direct3D 11 HLSL GPU):**
   - **تنعيم البشرة الذكي (Edge-Preserving Bilateral + Skin Mask):** ينعّم البشرة مع الحفاظ على حدة العيون والشعر واللحية.
   - **إضاءة استوديو (Studio Key-Light)** + تقليل تشويش الإضاءة الضعيفة + توضيح الملامح.
   - **12 فلتراً احترافياً جاهزاً بضغطة زر** + تحكم كامل في `Exposure`, `Contrast`, `Highlights`, `Shadows`, `Temperature`, `Tint`, `Saturation`, `Vibrance`, `Gamma`, `Vignette` + حفظ فلاتر مخصصة + **مقارنة قبل/بعد (Before/After Split)**.
5. **حل نهائي لمشكلة عكس الكاميرا والاتجاه في WhatsApp وTelegram:**
   - **أوضاع منصات جاهزة (Platform Mirror Profiles):** `WhatsApp`، `Telegram`، `Zoom / Meet / Teams`، `Raw`.
   - **ذاكرة منفصلة لكل كاميرا (Per-Camera Memory):** يحفظ إعداد الانعكاس والدوران للكاميرا الأمامية بشكل مستقل عن الخلفية.
   - **توضيح المعاينة:** تبديل بين *"معاينة مرآة لك"* و*"كما يراك الطرف الآخر بالضبط"*.
   - **زر Mirror Check (5 ثوانٍ):** يطبع نصاً غير متماثل (`123 ABC ->`) على خرج الكاميرا للتأكد الفوري داخل المكالمة.
   - **اختصار عالمي (`Ctrl+Shift+M`):** يعكس الكاميرا فوراً أثناء مكالمة واتساب أو تيلجرام بدون الخروج من نافذة المكالمة!

---

## التثبيت والتشغيل السريع (Quick Start)

1. نزّل أحدث إصدار من صفحة **Releases** على GitHub:
   - **`PhoneCam-Android-release.apk`** على موبايل الأندرويد.
   - **`PhoneCamStudio-Setup-x64.exe`** على جهاز Windows 11/10 (أو `PhoneCamStudio-Portable-x64.zip`).
2. شغّل `PhoneCamStudio-Setup-x64.exe` كـ **Administrator** مرة واحدة فقط لتسجيل الكاميرا الافتراضية على النظام.
3. افتح **PhoneCam Sender** على الأندرويد واضغط **بدء البث (Start Stream)**.
4. افتح **PhoneCam Studio** على ويندوز:
   - عبر **Wi-Fi**: سيظهر موبايلك تلقائياً في القائمة، أدخل الـ PIN واضغط **اتصال**.
   - عبر **USB**: فعّل USB Debugging وصل الكابل واضغط **اتصال USB**.
5. اضغط **تشغيل الكاميرا الافتراضية (Start Virtual Camera)**، ثم افتح **WhatsApp** أو **Telegram** واختر كاميرا **`PhoneCam Studio`**.

---

## حل مشكلة عكس الكاميرا في واتساب وتيلجرام

- في بعض التطبيقات (خصوصاً **WhatsApp Desktop** و**Telegram**)، نافذة المعاينة الصغيرة الخاصة بك تظهر معكوسة مثل المرآة، بينما يرى الطرف الآخر الصورة معتدلة — أو العكس حسب نوع الكاميرا (أمامية/خلفية).
- داخل تبويب **عكس الكاميرا والاتجاه**:
  1. اختر وضع المنصة المناسب (`WhatsApp` أو `Telegram`).
  2. بدّل المعاينة في الشريط العلوي إلى **"كما يراك الطرف الآخر بالضبط"**.
  3. اضغط **"اختبار الانعكاس (5s)"** وتأكد أن عبارة `123 ABC ->` مقروءة بشكل صحيح داخل المكالمة، أو اضغط **`Ctrl+Shift+M`** في أي وقت لعكس الاتجاه فوراً.

---

## Architecture & Build

راجع:
- [ARCHITECTURE.md](ARCHITECTURE.md)
- [DEVELOPMENT.md](DEVELOPMENT.md)
- [docs/PROTOCOL.md](docs/PROTOCOL.md)
