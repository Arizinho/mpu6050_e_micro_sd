import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator
import csv

# Lista para armazenar os dados
numero_amostra = []
accel_x = []
accel_y = []
accel_z = []
giro_x = []
giro_y = []
giro_z = []

# Leitura dos dados do arquivo CSV
with open('ArquivosDados/mpu6050_data.csv', newline='', encoding='utf-8') as csvfile:
    leitor = csv.DictReader(csvfile, delimiter=';') 
    for linha in leitor:
        numero_amostra.append(linha['numero_amostra'])
        accel_x.append(float(linha['accel_x']))
        accel_y.append(float(linha['accel_y']))
        accel_z.append(float(linha['accel_z']))
        giro_x.append(float(linha['giro_x']))
        giro_y.append(float(linha['giro_y']))
        giro_z.append(float(linha['giro_z']))

# Plot aceleração
plt.figure(figsize=(12, 5))
plt.subplot(1, 2, 1)
plt.plot(numero_amostra, accel_x, label='accel_x')
plt.plot(numero_amostra, accel_y, label='accel_y')
plt.plot(numero_amostra, accel_z, label='accel_z')
plt.xlabel('Amostra')
plt.ylabel('Aceleração (g)')
plt.legend()
plt.xlim(0, numero_amostra[-1])
plt.gca().xaxis.set_major_locator(MaxNLocator(integer=True, nbins=10))
plt.grid()

# Plot giro
plt.subplot(1, 2, 2)
plt.plot(numero_amostra, giro_x, label='giro_x')
plt.plot(numero_amostra, giro_y, label='giro_y')
plt.plot(numero_amostra, giro_z, label='giro_z')
plt.xlabel('Amostra')
plt.ylabel('Giro')
plt.legend()
plt.xlim(0, numero_amostra[-1])
plt.gca().xaxis.set_major_locator(MaxNLocator(integer=True, nbins=10))
plt.grid()
plt.show()

