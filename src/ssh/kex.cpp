//
// C++ Implementation: sshkex
//
// Description:
//
//
// Author: hooey <hephooey@gmail.com>, (C) 2007
//
// Copyright: See COPYING file that comes with this distribution
//
//

#include <QCryptographicHash>
#include <QtDebug>

#include "kex.h"
#include "packet.h"
#include "transport.h"
#include "ssh2.h"
#include "ssh1.h"
#include <openssl/rand.h>
#include <openssl/dsa.h>
#include <openssl/rsa.h>

#define INTBLOB_LEN     20
#define SSH_CIPHER_3DES    3
extern void dumpData(const QByteArray & data);

namespace QTerm
{

SSH2Kex::SSH2Kex(QByteArray * sessionID , SSH2InBuffer * in, SSH2OutBuffer * out, const QByteArray & server, const QByteArray & client, QObject * parent)
        : QObject(parent), V_S(server), V_C(client), I_S(), I_C(), m_status(Init)
{

    m_sessionID = sessionID;
    m_in = in;
    m_out = out;
    m_inTrans = NULL;
    m_outTrans = NULL;
    connect(m_in, SIGNAL(packetReady(int)), this, SLOT(kexPacketReceived(int)));

    ctx = BN_CTX_new();
    g = BN_new();
    p = BN_new();
    x = BN_new(); /* Random from client */
    e = BN_new(); /* g^x mod p */
    f = BN_new(); /* g^(Random from server) mod p */
    K = BN_new(); /* The shared secret: f^x mod p */

}

SSH2Kex::~SSH2Kex()
{
    BN_clear_free(g);
    BN_clear_free(p);
    BN_clear_free(x);
    BN_clear_free(e);
    BN_clear_free(f);
    BN_clear_free(K);
    BN_CTX_free(ctx);
}

void SSH2Kex::sendKex()
{
//  I_S = in->buffer();
//  m_encCS = m_encSC = "3des-cbc";
//  m_macCS = m_macSC = "hmac-sha1";
//  m_compCS = m_compSC = "none";
    QByteArray cookie;

    m_out->startPacket(SSH2_MSG_KEXINIT);
    cookie.resize(16);

    RAND_bytes((unsigned char *) cookie.data(), 16);

    m_out->putData(cookie);
    m_out->putString("diffie-hellman-group1-sha1,diffie-hellman-group14-sha1");
    m_out->putString("ssh-dss");
    m_out->putString("aes128-cbc");
    m_out->putString("aes128-cbc");
    m_out->putString("hmac-sha1");
    m_out->putString("hmac-sha1");
    m_out->putString("none");
    m_out->putString("none");
    m_out->putString("");
    m_out->putString("");
    m_out->putUInt8(0);
    m_out->putUInt32(0);

    I_C = m_out->buffer();

    m_out->sendPacket();
    m_status = KexSent;

}

void SSH2Kex::sendKexDH()
{
    unsigned char p_value[128] = {
                                     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                     0xC9, 0x0F, 0xDA, 0xA2, 0x21, 0x68, 0xC2, 0x34,
                                     0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1,
                                     0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74,
                                     0x02, 0x0B, 0xBE, 0xA6, 0x3B, 0x13, 0x9B, 0x22,
                                     0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
                                     0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B,
                                     0x30, 0x2B, 0x0A, 0x6D, 0xF2, 0x5F, 0x14, 0x37,
                                     0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45,
                                     0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6,
                                     0xF4, 0x4C, 0x42, 0xE9, 0xA6, 0x37, 0xED, 0x6B,
                                     0x0B, 0xFF, 0x5C, 0xB6, 0xF4, 0x06, 0xB7, 0xED,
                                     0xEE, 0x38, 0x6B, 0xFB, 0x5A, 0x89, 0x9F, 0xA5,
                                     0xAE, 0x9F, 0x24, 0x11, 0x7C, 0x4B, 0x1F, 0xE6,
                                     0x49, 0x28, 0x66, 0x51, 0xEC, 0xE6, 0x53, 0x81,
                                     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    BN_set_word(g, 2);
    BN_bin2bn(p_value, 128, p);
    // TODO: group_order = 128
    BN_rand(x, 128, 0, 0);
    BN_mod_exp(e, g, x, p, ctx);

    m_out->startPacket(SSH2_MSG_KEXDH_INIT);
    m_out->putBN(e);
    m_out->sendPacket();
}

void SSH2Kex::readKexInit()
{
    // TODO: select proper algorithm
    m_inTrans = new SSH2Transport("aes128-cbc", "hmac-sha1", "none");
    m_outTrans = new SSH2Transport("aes128-cbc", "hmac-sha1", "none");

    I_S = m_in->buffer();
    if (m_status == Init)
        sendKex();
    sendKexDH();
}

void SSH2Kex::kexPacketReceived(int flag)
{
    switch (flag) {
    case SSH2_MSG_KEXINIT:
        readKexInit();
        break;
    case SSH2_MSG_KEXDH_REPLY:
        readKexReply();
        break;
    case SSH2_MSG_NEWKEYS:
        if (m_status == NewKeysSent) {
            m_in->setTransport(m_inTrans);
            m_out->setTransport(m_outTrans);
            emit kexFinished();
        } else
            m_status = NewKeysReceived;
        break;
    default:
        break;
    }
}

void SSH2Kex::readKexReply()
{
    m_in->getUInt8();
    K_S = m_in->getString();
    m_in->getBN(f);
    QByteArray sign = m_in->getString();
    m_in->atEnd();

    BN_mod_exp(K, f, x, p, ctx);
    SSH2OutBuffer tmp(NULL);
    tmp.startPacket();
    tmp.putString(V_C.left(V_C.size() - 2));
    tmp.putString(V_S.left(V_S.size() - 1));
    tmp.putString(I_C);
    tmp.putString(I_S);
    tmp.putString(K_S);
    tmp.putBN(e);
    tmp.putBN(f);
    tmp.putBN(K);

    QByteArray key = QCryptographicHash::hash(tmp.buffer(), QCryptographicHash::Sha1);

    // TODO: error handling
    if (!verifySignature(key, K_S, sign))
        qDebug() << "Signature check error";

    m_out->startPacket(SSH2_MSG_NEWKEYS);
    m_out->sendPacket();

    if (m_sessionID == NULL)
        m_sessionID = new QByteArray(key);
    initTransport(key);

    if (m_status == NewKeysReceived) {
        m_in->setTransport(m_inTrans);
        m_out->setTransport(m_outTrans);
        emit kexFinished();
    } else
        m_status = NewKeysSent;
}

void SSH2Kex::initTransport(const QByteArray & hash)
{
    // client to server
    QByteArray iv = deriveKey(hash, m_sessionID, 'A', m_outTrans->ivLen());
    QByteArray secret = deriveKey(hash, m_sessionID, 'C', m_outTrans->secretLen());
    QByteArray key = deriveKey(hash, m_sessionID, 'E', m_outTrans->keyLen());
    // dumpData ( key );
    m_outTrans->initEncryption(secret, iv, SSH2Encryption::Encryption);
    m_outTrans->initMAC(key);
    //server to client
    iv = deriveKey(hash, m_sessionID, 'B', m_inTrans->ivLen());
    secret = deriveKey(hash, m_sessionID, 'D', m_inTrans->secretLen());
    key = deriveKey(hash, m_sessionID, 'F', m_inTrans->keyLen());
    // dumpData ( key );
    m_inTrans->initEncryption(secret, iv, SSH2Encryption::Decryption);
    m_inTrans->initMAC(key);
}

bool SSH2Kex::verifySignature(const QByteArray & hash, const QByteArray & hostKey, const QByteArray & signature)
{
    int ret = 0;
    SSH2InBuffer tmp(NULL);
    tmp.buffer().append(hostKey);
    QByteArray type = tmp.getString();
    qDebug() << "key type: " << type;

    if (type == "ssh-dss") {
        qDebug() << "generate DSA key";
        DSA *dsa = DSA_new();
        dsa->p = BN_new();
        dsa->q = BN_new();
        dsa->g = BN_new();
        dsa->pub_key = BN_new();
        tmp.getBN(dsa->p);
        tmp.getBN(dsa->q);
        tmp.getBN(dsa->g);
        tmp.getBN(dsa->pub_key);
        tmp.atEnd();

        tmp.buffer().append(signature);
        type.resize(0);
        type = tmp.getString();
        QByteArray signBlob = tmp.getString();
        //dumpData ( signBlob );

        if (type == "ssh-dss") {
            qDebug() << "generate DSA signature";
            DSA_SIG * sig;
            sig = DSA_SIG_new();
            sig->r = BN_new();
            sig->s = BN_new();

            BN_bin2bn((const unsigned char *) signBlob.data(), INTBLOB_LEN, sig->r);

            BN_bin2bn((const unsigned char *) signBlob.data() + INTBLOB_LEN, INTBLOB_LEN, sig->s);

            QByteArray digest = QCryptographicHash::hash(hash, QCryptographicHash::Sha1);

            ret = DSA_do_verify((const unsigned char *) digest.data(), digest.size(), sig, dsa);

            DSA_SIG_free(sig);

        }

        DSA_free(dsa);
    }

    if (ret == 1)
        return true;
    else if (ret == 0)
        qDebug() << "incorrect";
    else
        qDebug() << "error: " << ret;

    return false;
}

QByteArray SSH2Kex::deriveKey(const QByteArray & hash, const QByteArray * sessionID, char id, uint need)
{
    SSH2OutBuffer tmp(0);
    tmp.startPacket();
    tmp.putBN(K);
    QCryptographicHash sha1Hash(QCryptographicHash::Sha1);
    sha1Hash.addData(tmp.buffer());
    sha1Hash.addData(hash);
    sha1Hash.addData(&id, 1);
    sha1Hash.addData(sessionID->data(), sessionID->size());
    QByteArray digest = sha1Hash.result();
    for (uint have = 20; need > have; have += need) {
        sha1Hash.reset();
        sha1Hash.addData(tmp.buffer());
        sha1Hash.addData(hash);
        sha1Hash.addData(digest);
        digest += sha1Hash.result();
    }
    // digest.truncate ( need );
    return digest;
}
SSH1Kex::SSH1Kex(SSH1InBuffer * in, SSH1OutBuffer * out, QObject * parent)
        : QObject(parent), m_cookie(), m_sessionID(), m_sessionKey()
{
    m_in = in;
    m_out = out;
//  m_inTrans = NULL;
//  m_outTrans = NULL;
    connect(m_in, SIGNAL(packetReady(int)), this, SLOT(kexPacketReceived(int)));
}

SSH1Kex::~SSH1Kex()
{}

void SSH1Kex::kexPacketReceived(int flag)
{
    qDebug() << "flag: " << flag;
    switch (flag) {
    case SSH_SMSG_PUBLIC_KEY:
        readKex();
        break;
    case SSH_SMSG_SUCCESS:
//    m_out->startPacket(SSH_CMSG_USER);
//    m_out->putString("hooey");
//    m_out->sendPacket();
        emit kexFinished();
        break;
    default: {
        qDebug() << "unknown packet";
        m_in->getUInt8();
        QString error = m_in->getString();
        qDebug() << "error: " << error;
        break;
    }
    }
}

void SSH1Kex::readKex()
{
    m_in->getUInt8();
    m_cookie = m_in->getData(8);
    int serverKeyLength = m_in->getUInt32();
    qDebug() << "length" << serverKeyLength;

    RSA * serverKey = RSA_new();
    serverKey->e = BN_new();
    serverKey->n = BN_new();
    m_in->getBN(serverKey->e);
    m_in->getBN(serverKey->n);
    QByteArray server_n(BN_num_bytes(serverKey->n), 0);
    BN_bn2bin(serverKey->n, (unsigned char *) server_n.data());
    // TODO: rbits = BN_num_bits(d_servKey->d_rsa->n);

    int hostKeyLength = m_in->getUInt32();
    qDebug() << "length" << hostKeyLength;
    RSA * hostKey = RSA_new();
    hostKey->e = BN_new();
    hostKey->n = BN_new();
    m_in->getBN(hostKey->e);
    m_in->getBN(hostKey->n);
    QByteArray host_n(BN_num_bytes(hostKey->n), 0);
    BN_bn2bin(hostKey->n, (unsigned char *) host_n.data());
    m_sessionID = QCryptographicHash::hash(host_n + server_n + m_cookie, QCryptographicHash::Md5);
    dumpData(m_sessionID);

    m_sessionKey.resize(32);
    RAND_bytes((unsigned char *) m_sessionKey.data(), 32);
    initEncryption(m_sessionKey);
    dumpData(m_sessionKey);

    BIGNUM * key = BN_new();
    BN_set_word(key, 0);
    for (int i = 0; i < 32; i++) {
        BN_lshift(key, key, 8);
        if (i < 16)
            BN_add_word(key, ((u_char)m_sessionKey[i]) ^((u_char)m_sessionID[i]));
        else
            BN_add_word(key, (u_char)m_sessionKey[i]);
    }
    if (BN_cmp(serverKey->n, hostKey->n) < 0) {
        publicEncrypt(key, key, serverKey);
        publicEncrypt(key, key, hostKey);
    } else {
        publicEncrypt(key, key, hostKey);
        publicEncrypt(key, key, serverKey);
    }

    uint serverFlag = m_in->getUInt32();
    uint serverCiphers = m_in->getUInt32();
    uint serverAuth = m_in->getUInt32();
    if ((serverCiphers & (1 << SSH_CIPHER_3DES)) != 0)
        qDebug() << "We can use 3DES";
//  qDebug() << serverFlag << serverCiphers << serverAuth;
    m_in->atEnd();
    m_out->startPacket(SSH_CMSG_SESSION_KEY);
    m_out->putUInt8(SSH_CIPHER_3DES);
    m_out->putData(m_cookie);
    m_out->putBN(key);
    m_out->putUInt32(1);
    m_out->sendPacket();
    m_in->setEncryption(m_inEncrypt);
    m_out->setEncryption(m_outEncrypt);
}

void SSH1Kex::publicEncrypt(BIGNUM *out, BIGNUM *in, RSA *key)
{
    QByteArray inbuf;
    QByteArray outbuf;
    int len, ilen, olen;

    if (BN_num_bits(key->e) < 2 || !BN_is_odd(key->e))
        qDebug() << "rsa_public_encrypt() exponent too small or not odd";

    olen = BN_num_bytes(key->n);
    outbuf.resize(olen);

    ilen = BN_num_bytes(in);
    inbuf.resize(ilen);
    BN_bn2bin(in, (unsigned char *) inbuf.data());

    if ((len = RSA_public_encrypt(ilen, (unsigned char *) inbuf.data(), (unsigned char *) outbuf.data(), key,
                                  RSA_PKCS1_PADDING)) <= 0)
        qDebug() << "rsa_public_encrypt() failed";

    if (BN_bin2bn((unsigned char *) outbuf.data(), len, out) == NULL)
        qDebug() << "rsa_public_encrypt: BN_bin2bn failed";

}

void SSH1Kex::initEncryption(const QByteArray & key)
{
    m_inEncrypt = new SSH1Encryption(SSH1Encryption::Decryption, key);
    m_outEncrypt = new SSH1Encryption(SSH1Encryption::Encryption, key);
}

}

#include "kex.moc"
