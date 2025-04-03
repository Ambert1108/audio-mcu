# SIP 改造方案

## 背景

在MCU现有版本的基础上，使用核心网中常见的SIP协议进行信令部分改造



## 目标

- 信令流程改用标准SIP协议实现，参考RFC3261



## 方案

- 创建任务使用`MESSAGE`和`REGISTER`实现。信令以jobId作为用户名注册信令任务账号，向MCU发送`MESSAGE`告知MCU以audio+jobId作为用户名注册MCU任务账号，同时提供音频编码格式和视频MCU回调账号，MCU成功处理后回复`200 OK`
- 添加通道使用`INVITE`实现，其中To为audio+jobId，From中的用户名作为channelId，MCU从sdp中获取payloadType、目的IP和端口。MCU成功处理后回复`180 Ringing`和`200 OK`
- 移除通道使用`BYE`实现，其中To为audio+jobId，From中的用户名作为channelId，用于区分通道。MCU成功处理后回复`200 OK`
- 开关麦克风均使用`INFO`实现，成功回复`200 OK`
- 结束任务使用`MESSAGE`实现成功回复`200 OK`



### 时序图

#### 创建会议

```mermaid
sequenceDiagram
    actor user
  	participant signal
  	participant mcu
  Note over user,mcu:创建任务
  user->>signal:INVITE F1
  
  signal->>signal:创建 roomId
  
  signal->>signal:REGISTER <sip:roomId>
  
  Note over signal,mcu: 通知MCU使用audio_jobId注册
  signal->>mcu: MESSAGE F2
  mcu->>signal: REGISTER F3
  signal-->>mcu:200 OK(REGISTER) F4
  mcu-->>signal: 200 OK(MESSAGE) F5
  
  Note over user,mcu: 创建通道
  signal->>mcu: INVITE F6
  signal-->>user: 100 Trying F7
  mcu-->>signal: 180 Ringing F8
  signal-->>user: 180 Ringing F9
  mcu-->>signal: 200 OK(INVITE) F10
  signal-->>user: 200 OK(INVITE) F11
  user->>signal:ACK F12
  signal->>mcu:ACK F13

```

#### 加入会议

```mermaid
sequenceDiagram
    actor user
  	participant signal
  	participant mcu
  	
  	
  Note over user,mcu: 加入会议
  user->>signal:INVITE F1
 
  signal->>mcu: INVITE F2
  signal-->>user: 100 Trying F3
  mcu-->>signal: 180 Ringing F34
  signal-->>user: 180 Ringing F45
  mcu-->>signal: 200 OK(INVITE) F56
  signal-->>user: 200 OK(INVITE) F67
  user->>signal:ACK F78
  signal->>mcu:ACK F89

```

#### 离开会议

```mermaid
sequenceDiagram
    actor user
  	participant signal
  	participant mcu
  
  Note over user,mcu: 离开会议
  user->>signal: BYE F1
  signal->>mcu: BYE F2
  mcu-->>signal: 200 OK(BYE) F3
  signal-->>user: 200 OK(BYE) F4
  
  signal->>signal:判断房间内是否还有用户
  signal->>mcu:MESSAGE F5
  mcu->>mcu:destory "video+roomId"
  mcu-->>signal: 200 OK(MESSAGE) F6
  signal->>signal: destory "roonmId"

```
