/**
 * Production-Grade Message & Error Localization Engine for Aeroponics Smart Farm
 *
 * Provides user-friendly, actionable Vietnamese messages for IoT and dashboard operations.
 * Strictly adheres to:
 *  - S4-DS-ICON-14: Zero emoji across all strings
 *  - Professional agricultural & automation terminology
 */

export interface FormattedError {
  title: string;
  message: string;
  hint?: string;
}

/**
 * Common translation dictionary for technical NestJS validation & API errors
 */
const KNOWN_ERROR_TRANSLATIONS: Record<string, string> = {
  // Authentication
  'Unauthorized': 'Phiên đăng nhập đã hết hạn hoặc không có quyền truy cập.',
  'Invalid credentials': 'Tên đăng nhập hoặc mật khẩu không chính xác.',
  'Token is required': 'Yêu cầu mã xác thực phiên truy cập.',
  'Invalid request': 'Yêu cầu không hợp lệ.',

  // Season
  'Season not found': 'Không tìm thấy thông tin vụ mùa trong hệ thống.',
  'Active season already exists': 'Hiện đã có một vụ mùa đang hoạt động. Vui lòng kết thúc vụ mùa cũ trước khi tạo mới.',
  'No active season': 'Hiện tại chưa có vụ mùa nào đang được kích hoạt.',

  // Group & Node
  'Group not found': 'Không tìm thấy nhóm điều khiển yêu cầu.',
  'Node not found': 'Không tìm thấy trạm viễn thám yêu cầu.',
  'Invalid ID, Name required': 'Mã định danh không hợp lệ hoặc thiếu tên cấu hình.',

  // Treatment & Version
  'Treatment not found': 'Không tìm thấy công thức khí canh yêu cầu.',
  'Version not found': 'Không tìm thấy phiên bản công thức yêu cầu.',
  'Version is already published': 'Phiên bản công thức này đã được phát hành trước đó.',
  'Version must be published': 'Chỉ có thể gán công thức đã được phát hành (PUBLISHED).',

  'name should not be empty': 'Tên không được để trống.',
  'target_ec must be a positive number': 'Chỉ số EC mục tiêu phải là số dương hợp lệ.',
  'target_ph must be a positive number': 'Độ pH mục tiêu phải là số dương hợp lệ.',

  // Tuya Bridge Probe Protection
  'TUYA_BRIDGE_DISABLED': 'Thiết bị đo chất lượng nước Tuya PH-W218 đang ở chế độ TẮT để bảo quản đầu dò pH/EC/ORP. Vui lòng kích hoạt thiết bị trước khi đo.',
};

/**
 * Maps raw backend/technical errors into polished, actionable Vietnamese error objects.
 */
export function formatUserErrorMessage(
  error: unknown,
  fallbackContext = 'Thao tác không thành công',
): FormattedError {
  if (!error) {
    return {
      title: fallbackContext,
      message: 'Đã xảy ra sự cố không xác định. Vui lòng thử lại.',
    };
  }

  // Handle strings directly
  if (typeof error === 'string') {
    const translated = KNOWN_ERROR_TRANSLATIONS[error] || error;
    return {
      title: fallbackContext,
      message: translated,
    };
  }

  // Handle objects and Errors
  const errObj = error as any;
  const status: number | undefined = errObj.status ?? errObj.statusCode;
  const rawMessage: string = errObj.message || errObj.statusText || '';

  // Network or connection drops
  if (
    rawMessage.includes('Failed to fetch') ||
    rawMessage.includes('NetworkError') ||
    rawMessage.includes('fetch failed') ||
    errObj.name === 'TypeError'
  ) {
    return {
      title: 'Mất kết nối mạng',
      message: 'Không thể kết nối đến máy chủ hoặc cổng Gateway viễn thám.',
      hint: 'Vui lòng kiểm tra lại đường truyền mạng LAN hoặc kết nối Internet của bạn.',
    };
  }

  // Rate limit / Cooldown (Tuya sensor protection)
  if (status === 429 || rawMessage.includes('429') || rawMessage.toLowerCase().includes('rate limit')) {
    return {
      title: 'Thiết bị đang làm nguội',
      message: 'Đầu dò cảm biến chất lượng nước đang trong chu kỳ bảo vệ 60 giây.',
      hint: 'Vui lòng chờ hết đồng hồ đếm ngược trước khi gửi lệnh đo tiếp theo.',
    };
  }

  // Unauthorized (401)
  if (status === 401 || rawMessage === 'Unauthorized') {
    return {
      title: 'Phiên đăng nhập hết hạn',
      message: 'Phiên làm việc đã kết thúc. Hệ thống sẽ tự động chuyển về màn hình đăng nhập.',
      hint: 'Vui lòng đăng nhập lại để tiếp tục thao tác.',
    };
  }

  // Forbidden (403)
  if (status === 403) {
    return {
      title: 'Không có quyền truy cập',
      message: 'Tài khoản của bạn không có thẩm quyền thực hiện thay đổi cấu hình này.',
      hint: 'Vui lòng liên hệ quản trị viên nông trại để được cấp quyền.',
    };
  }

  // Not Found (404)
  if (status === 404) {
    return {
      title: 'Không tìm thấy dữ liệu',
      message: KNOWN_ERROR_TRANSLATIONS[rawMessage] || 'Dữ liệu hoặc trạm điều khiển này không tồn tại trên hệ thống.',
      hint: 'Dữ liệu có thể đã bị xóa hoặc cập nhật bởi người dùng khác.',
    };
  }

  // Conflict (409)
  if (status === 409) {
    return {
      title: 'Xung đột dữ liệu',
      message: KNOWN_ERROR_TRANSLATIONS[rawMessage] || 'Trạng thái hiện tại của thiết bị xung đột với yêu cầu gửi đến.',
      hint: 'Vui lòng tải lại trang hoặc kiểm tra trạng thái vụ mùa/công thức trước khi thử lại.',
    };
  }

  // Validation Error (400 / 422)
  if (status === 400 || status === 422) {
    let cleanMessage = rawMessage;
    if (KNOWN_ERROR_TRANSLATIONS[rawMessage]) {
      cleanMessage = KNOWN_ERROR_TRANSLATIONS[rawMessage];
    } else if (rawMessage.startsWith('API Error 400:')) {
      cleanMessage = 'Thông tin nhập vào chưa hợp lệ. Vui lòng kiểm tra lại các trường dữ liệu.';
    }

    return {
      title: 'Dữ liệu không hợp lệ',
      message: cleanMessage,
      hint: 'Vui lòng kiểm tra và điền đầy đủ các thông số bắt buộc theo yêu cầu.',
    };
  }

  // Hardware & Sensor Gateway Failure (502 Bad Gateway / Tuya communication)
  if (
    status === 502 ||
    rawMessage.includes('502') ||
    rawMessage.includes('Bad Gateway') ||
    rawMessage.includes('Tuya sensor') ||
    rawMessage.includes('Tuya device') ||
    rawMessage.includes('Tuya credentials')
  ) {
    return {
      title: 'Không thể kết nối cảm biến đo nước (Tuya PH-W218)',
      message:
        'Cổng điều khiển không nhận được phản hồi từ cảm biến đo nước qua mạng cục bộ.',
      hint:
        'Vui lòng kiểm tra nguồn điện đầu dò PH-W218, kết nối WiFi nội bộ của thiết bị hoặc cấu hình địa chỉ IP.',
    };
  }

  // Request Timeout (408 / 504)
  if (
    status === 408 ||
    status === 504 ||
    rawMessage.toLowerCase().includes('timed out') ||
    rawMessage.toLowerCase().includes('timeout')
  ) {
    return {
      title: 'Hết thời gian chờ phản hồi',
      message: 'Thiết bị cảm biến hoặc máy chủ phản hồi quá thời gian quy định.',
      hint: 'Vui lòng kiểm tra khoảng cách kết nối không dây hoặc khởi động lại thiết bị đo.',
    };
  }

  // Server Errors (500, 503)
  if (status && status >= 500) {
    return {
      title: 'Lỗi máy chủ nội bộ',
      message: 'Dịch vụ xử lý viễn thám gặp sự cố trong khi xử lý lệnh.',
      hint: 'Hệ thống đang tự động phục hồi. Vui lòng thử lại sau giây lát hoặc liên hệ kỹ thuật.',
    };
  }


  // Fallback for general errors
  const translated = KNOWN_ERROR_TRANSLATIONS[rawMessage] || rawMessage;
  return {
    title: fallbackContext,
    message: translated || 'Đã xảy ra lỗi trong quá trình thực hiện. Vui lòng thử lại.',
    hint: 'Nếu sự cố vẫn tiếp diễn, vui lòng kiểm tra kết nối với trạm điều khiển.',
  };
}

/**
 * Standardized Success Messages
 */
export const SUCCESS_MESSAGES = Object.freeze({
  ASSIGN_GROUP: (groupId: number) => `Cập nhật cấu hình Nhóm #${groupId} thành công.`,
  UNASSIGN_GROUP: (groupId: number) => `Đã hủy gán cấu hình cho Nhóm #${groupId}.`,
  CREATE_SEASON: (name: string) => `Khởi tạo vụ mùa "${name}" thành công.`,
  END_SEASON: (name: string) => `Đã kết thúc vụ mùa "${name}" thành công.`,
  CREATE_TREATMENT: (name: string) => `Tạo công thức khí canh "${name}" thành công.`,
  CREATE_VERSION: (name: string, ver: number) => `Tạo phiên bản v${ver} cho công thức "${name}" thành công.`,
  PUBLISH_VERSION: (ver: number) => `Đã phát hành phiên bản v${ver}. Bạn có thể gán vào nhóm trạm ngay bây giờ.`,
  RESET_FAULT: (nodeName: string) => `Đã gửi lệnh khôi phục lỗi cho ${nodeName} thành công.`,
  TRIGGER_MEASUREMENT: 'Đã kích hoạt phiên lấy mẫu dung dịch viễn thám.',
  TOGGLE_TUYA_ENABLED: 'Đã kích hoạt thiết bị đo Tuya PH-W218 (Sẵn sàng lấy mẫu).',
  TOGGLE_TUYA_DISABLED: 'Đã chuyển thiết bị Tuya PH-W218 sang chế độ bảo quản đầu dò.',
  PUMP_OVERRIDE_ON: (nodeName: string, leaseSec: number) =>
    `Đã gửi lệnh bật bơm cưỡng bức cho ${nodeName} (${leaseSec} giây).`,
  PUMP_OVERRIDE_OFF: (nodeName: string) =>
    `Đã gửi lệnh tắt bơm cưỡng bức cho ${nodeName}.`,
} as const);
